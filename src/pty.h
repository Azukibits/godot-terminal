#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace godot {

// Abstract pty backend. Concrete impls: PtyWindows (ConPTY) and PtyUnix
// (forkpty). All string arguments are UTF-8.
class IPty {
public:
    virtual ~IPty() = default;

    // Spawn `exe` with `args` attached to a new pseudoconsole/pty of the
    // requested size. `cwd` empty means inherit the host process cwd.
    // Returns false on failure.
    virtual bool start(const std::string &exe,
                       const std::vector<std::string> &args,
                       int cols, int rows,
                       const std::string &cwd = std::string()) = 0;

    // Kill the child, close handles/fds, join the reader thread.
    virtual void stop() = 0;

    // True between a successful start() and the child exiting.
    virtual bool is_running() const = 0;

    // True once the child has terminated and the reader thread has drained EOF.
    virtual bool exited() const = 0;
    virtual int exit_code() const = 0;

    // Send bytes to the child's stdin.
    virtual void write(const char *data, std::size_t len) = 0;

    // Resize the pseudoconsole / pty. No-op if not running.
    virtual void resize(int cols, int rows) = 0;

    // Drain accumulated child stdout/stderr bytes. The callback is invoked on
    // the calling thread with a contiguous span of bytes valid for the
    // duration of the call.
    using DrainFn = std::function<void(const char *, std::size_t)>;
    virtual void drain(const DrainFn &cb) = 0;
};

// Creates the right backend for the host platform.
std::unique_ptr<IPty> make_pty();

} // namespace godot
