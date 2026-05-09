#ifndef _WIN32

// glibc gates forkpty / openpty on _GNU_SOURCE / _DEFAULT_SOURCE; define
// it before any header is parsed so the build doesn't depend on the
// caller's CFLAGS.
#if !defined(__APPLE__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include "pty_unix.h"

#if defined(__APPLE__)
#include <util.h>
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
      defined(__DragonFly__)
#include <libutil.h>
#else
#include <pty.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <thread>
#include <vector>

namespace godot {

PtyUnix::PtyUnix() = default;
PtyUnix::~PtyUnix() { stop(); }

bool PtyUnix::start(const std::string &exe,
                    const std::vector<std::string> &args,
                    int cols, int rows,
                    const std::string &cwd) {
    if (running_.load()) return false;
    if (exe.empty()) return false;

    struct winsize ws = {};
    ws.ws_col = static_cast<unsigned short>(cols > 0 ? cols : 80);
    ws.ws_row = static_cast<unsigned short>(rows > 0 ? rows : 24);

    int master_fd = -1;
    pid_t pid = forkpty(&master_fd, nullptr, nullptr, &ws);
    if (pid < 0) return false;

    if (pid == 0) {
        // Child. After forkpty stdin/stdout/stderr are the pty slave.
        if (!cwd.empty()) {
            // chdir failure is not fatal — fall through to exec from
            // inherited cwd rather than aborting silently.
            (void)chdir(cwd.c_str());
        }

        // Default-friendly environment for TUI apps. TERM lets ncurses /
        // libvterm-style tools pick the right termcap; COLORTERM lights up
        // truecolor in many programs.
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        unsetenv("LINES");
        unsetenv("COLUMNS");

        // Build argv: exe is argv[0] by default unless caller already set it.
        std::vector<char *> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char *>(exe.c_str()));
        for (const std::string &a : args) {
            argv.push_back(const_cast<char *>(a.c_str()));
        }
        argv.push_back(nullptr);

        // Reset signal handlers/mask the parent may have set so the child
        // gets the standard defaults.
        sigset_t mask;
        sigemptyset(&mask);
        sigprocmask(SIG_SETMASK, &mask, nullptr);
        signal(SIGPIPE, SIG_DFL);

        execvp(exe.c_str(), argv.data());
        // execvp only returns on failure.
        _exit(127);
    }

    // Parent.
    master_fd_ = master_fd;
    child_pid_ = pid;
    exited_.store(false);
    exit_code_ = 0;
    running_.store(true);

    reader_ = std::thread([this]() { _reader_loop(); });
    return true;
}

void PtyUnix::stop() {
    bool was_running = running_.exchange(false);
    int pid_snapshot = child_pid_;

    // Polite signal first; closing master fd then wakes the reader.
    if (was_running && pid_snapshot > 0) {
        kill(pid_snapshot, SIGHUP);
    }
    _close_master();

    // Reader thread reaps the child and flips exited_ when done. Wait up
    // to ~200ms for that, then escalate to SIGKILL if the child is hung.
    for (int i = 0; i < 20 && !exited_.load(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!exited_.load() && was_running && pid_snapshot > 0) {
        kill(pid_snapshot, SIGKILL);
    }

    if (reader_.joinable()) reader_.join();
}

void PtyUnix::_close_master() {
    if (master_fd_ >= 0) {
        ::close(master_fd_);
        master_fd_ = -1;
    }
}

void PtyUnix::_reader_loop() {
    char buf[4096];
    while (true) {
        ssize_t n = ::read(master_fd_, buf, sizeof(buf));
        if (n > 0) {
            std::lock_guard<std::mutex> lk(buf_mu_);
            buf_.insert(buf_.end(), buf, buf + n);
            continue;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            // EIO on Linux when the slave closes (child exited).
            break;
        }
        break; // n == 0, EOF.
    }

    // Reap the child if it exited on its own (stop() may have already done it).
    if (child_pid_ > 0) {
        int status = 0;
        pid_t r = waitpid(child_pid_, &status, 0);
        if (r == child_pid_) {
            if (WIFEXITED(status))   exit_code_ = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) exit_code_ = 128 + WTERMSIG(status);
            child_pid_ = -1;
        }
    }

    exited_.store(true);
    running_.store(false);
}

bool PtyUnix::is_running() const {
    return running_.load() && !exited_.load();
}

void PtyUnix::write(const char *data, std::size_t len) {
    if (master_fd_ < 0 || data == nullptr || len == 0) return;
    // Loop in case of partial writes / EINTR.
    std::size_t off = 0;
    while (off < len) {
        ssize_t n = ::write(master_fd_, data + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        off += static_cast<std::size_t>(n);
    }
}

void PtyUnix::resize(int cols, int rows) {
    if (master_fd_ < 0) return;
    struct winsize ws = {};
    ws.ws_col = static_cast<unsigned short>(cols > 0 ? cols : 1);
    ws.ws_row = static_cast<unsigned short>(rows > 0 ? rows : 1);
    ioctl(master_fd_, TIOCSWINSZ, &ws);
}

void PtyUnix::drain(const DrainFn &cb) {
    std::vector<char> local;
    {
        std::lock_guard<std::mutex> lk(buf_mu_);
        if (buf_.empty()) return;
        local.swap(buf_);
    }
    cb(local.data(), local.size());
}

std::unique_ptr<IPty> make_pty() {
    return std::unique_ptr<IPty>(new PtyUnix());
}

} // namespace godot

#endif // !_WIN32
