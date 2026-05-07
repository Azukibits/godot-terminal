#pragma once

#ifdef _WIN32

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace godot {

// Wraps a ConPTY pseudoconsole + child process + reader thread.
// Win32 handles are stored as void* so <windows.h> stays out of this header.
class PtyWindows {
public:
    PtyWindows();
    ~PtyWindows();

    PtyWindows(const PtyWindows &) = delete;
    PtyWindows &operator=(const PtyWindows &) = delete;

    // Spawn `command_line` (e.g. L"cmd.exe" or L"powershell.exe -NoLogo")
    // attached to a new ConPTY of the given size. Returns false on failure.
    bool start(const std::wstring &command_line, int cols, int rows,
               const std::wstring &cwd = L"");

    // Kill the child, close handles, join the reader thread.
    void stop();

    // True between a successful start() and the child exiting.
    bool is_running() const;

    // True once the child has terminated and the reader thread has drained EOF.
    bool exited() const { return exited_.load(); }
    int exit_code() const { return exit_code_; }

    // Send bytes to the child's stdin.
    void write(const char *data, std::size_t len);

    // Resize the pseudoconsole. Does nothing if not running.
    void resize(int cols, int rows);

    // Drain accumulated stdout bytes. The callback is invoked on the
    // calling thread with a contiguous span of bytes owned by a temporary
    // local buffer (valid only for the duration of the call).
    using DrainFn = std::function<void(const char *, std::size_t)>;
    void drain(const DrainFn &cb);

private:
    // Opaque handles — actually HPCON / HANDLE.
    void *h_pc_ = nullptr;
    void *h_in_write_ = nullptr;     // child stdin pipe, write end
    void *h_out_read_ = nullptr;     // child stdout pipe, read end
    void *h_proc_ = nullptr;         // child process
    void *h_thread_handle_ = nullptr;// child main thread

    std::thread reader_;
    std::mutex buf_mu_;
    std::vector<char> buf_;

    std::atomic<bool> running_{false};
    std::atomic<bool> exited_{false};
    int exit_code_ = 0;

    void _reader_loop();
    void _close_handles();
};

} // namespace godot

#endif // _WIN32
