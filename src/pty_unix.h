#pragma once

#ifndef _WIN32

#include "pty.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <thread>

namespace godot {

// forkpty + execvp backed IPty implementation.
// Child stdin/stdout/stderr are wired to the pty slave; the master fd is
// kept here and read on a worker thread.
class PtyUnix final : public IPty {
public:
    PtyUnix();
    ~PtyUnix() override;

    PtyUnix(const PtyUnix &) = delete;
    PtyUnix &operator=(const PtyUnix &) = delete;

    bool start(const std::string &exe,
               const std::vector<std::string> &args,
               int cols, int rows,
               const std::string &cwd) override;

    void stop() override;
    bool is_running() const override;
    bool exited() const override { return exited_.load(); }
    int exit_code() const override { return exit_code_; }
    void write(const char *data, std::size_t len) override;
    void resize(int cols, int rows) override;
    void drain(const DrainFn &cb) override;

private:
    int master_fd_ = -1;
    int child_pid_ = -1;

    std::thread reader_;
    std::mutex buf_mu_;
    std::vector<char> buf_;

    std::atomic<bool> running_{false};
    std::atomic<bool> exited_{false};
    int exit_code_ = 0;

    void _reader_loop();
    void _close_master();
};

} // namespace godot

#endif // !_WIN32
