#ifdef _WIN32

#include "pty_windows.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>

namespace godot {

namespace {
inline HANDLE H(void *p) { return reinterpret_cast<HANDLE>(p); }
inline HPCON  P(void *p) { return reinterpret_cast<HPCON>(p); }
} // namespace

PtyWindows::PtyWindows() = default;
PtyWindows::~PtyWindows() { stop(); }

bool PtyWindows::start(const std::wstring &command_line, int cols, int rows,
                       const std::wstring &cwd) {
    if (running_.load()) return false;

    HANDLE hPipeIn = nullptr;       // child reads from this (handed to ConPTY)
    HANDLE hPipeInWrite = nullptr;  // host writes to this
    HANDLE hPipeOutRead = nullptr;  // host reads from this
    HANDLE hPipeOut = nullptr;      // child writes to this (handed to ConPTY)

    if (!CreatePipe(&hPipeIn, &hPipeInWrite, nullptr, 0)) {
        return false;
    }
    if (!CreatePipe(&hPipeOutRead, &hPipeOut, nullptr, 0)) {
        CloseHandle(hPipeIn);
        CloseHandle(hPipeInWrite);
        return false;
    }

    COORD size;
    size.X = static_cast<SHORT>(cols > 0 ? cols : 80);
    size.Y = static_cast<SHORT>(rows > 0 ? rows : 24);

    HPCON hPC = nullptr;
    HRESULT hr = CreatePseudoConsole(size, hPipeIn, hPipeOut, 0, &hPC);

    // ConPTY owns hPipeIn / hPipeOut. Close the host-side handles so the
    // pseudoconsole is the sole holder.
    CloseHandle(hPipeIn);
    CloseHandle(hPipeOut);

    if (FAILED(hr) || hPC == nullptr) {
        CloseHandle(hPipeInWrite);
        CloseHandle(hPipeOutRead);
        return false;
    }

    // Build STARTUPINFOEX with a single attribute: PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE.
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    LPPROC_THREAD_ATTRIBUTE_LIST attr_list =
        static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            HeapAlloc(GetProcessHeap(), 0, attr_size));
    if (attr_list == nullptr) {
        ClosePseudoConsole(hPC);
        CloseHandle(hPipeInWrite);
        CloseHandle(hPipeOutRead);
        return false;
    }

    if (!InitializeProcThreadAttributeList(attr_list, 1, 0, &attr_size) ||
        !UpdateProcThreadAttribute(attr_list, 0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   hPC, sizeof(HPCON), nullptr, nullptr)) {
        HeapFree(GetProcessHeap(), 0, attr_list);
        ClosePseudoConsole(hPC);
        CloseHandle(hPipeInWrite);
        CloseHandle(hPipeOutRead);
        return false;
    }

    STARTUPINFOEXW si = {};
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    si.lpAttributeList = attr_list;

    PROCESS_INFORMATION pi = {};
    // CreateProcessW takes a writable command-line buffer.
    std::wstring cmd = command_line;
    LPCWSTR cwd_ptr = cwd.empty() ? nullptr : cwd.c_str();

    BOOL ok = CreateProcessW(
        /*lpApplicationName*/ nullptr,
        /*lpCommandLine    */ cmd.empty() ? nullptr : cmd.data(),
        /*lpProcessAttrs   */ nullptr,
        /*lpThreadAttrs    */ nullptr,
        /*bInheritHandles  */ FALSE,
        /*dwCreationFlags  */ EXTENDED_STARTUPINFO_PRESENT,
        /*lpEnvironment    */ nullptr,
        /*lpCurrentDir     */ cwd_ptr,
        /*lpStartupInfo    */ &si.StartupInfo,
        /*lpProcInfo       */ &pi);

    DeleteProcThreadAttributeList(attr_list);
    HeapFree(GetProcessHeap(), 0, attr_list);

    if (!ok) {
        ClosePseudoConsole(hPC);
        CloseHandle(hPipeInWrite);
        CloseHandle(hPipeOutRead);
        return false;
    }

    h_pc_ = hPC;
    h_in_write_ = hPipeInWrite;
    h_out_read_ = hPipeOutRead;
    h_proc_ = pi.hProcess;
    h_thread_handle_ = pi.hThread;
    exited_.store(false);
    exit_code_ = 0;
    running_.store(true);

    // Start the reader thread *after* all handles are stored.
    reader_ = std::thread([this]() { _reader_loop(); });
    return true;
}

void PtyWindows::stop() {
    bool was_running = running_.exchange(false);

    // ClosePseudoConsole closes the pty-side pipe handles, which signals
    // EOF to the reader thread's blocking ReadFile.
    if (h_pc_ != nullptr) {
        ClosePseudoConsole(P(h_pc_));
        h_pc_ = nullptr;
    }

    // ClosePseudoConsole triggers a graceful close in most TUI apps; cmd.exe
    // with a running child sometimes needs an explicit terminate.
    if (h_proc_ != nullptr && was_running) {
        DWORD wait = WaitForSingleObject(H(h_proc_), 200);
        if (wait == WAIT_TIMEOUT) {
            TerminateProcess(H(h_proc_), 1);
            WaitForSingleObject(H(h_proc_), INFINITE);
        }
    }

    if (reader_.joinable()) reader_.join();

    _close_handles();
}

void PtyWindows::_close_handles() {
    if (h_in_write_) { CloseHandle(H(h_in_write_)); h_in_write_ = nullptr; }
    if (h_out_read_) { CloseHandle(H(h_out_read_)); h_out_read_ = nullptr; }
    if (h_proc_) {
        DWORD code = 0;
        if (GetExitCodeProcess(H(h_proc_), &code)) {
            exit_code_ = static_cast<int>(code);
        }
        CloseHandle(H(h_proc_));
        h_proc_ = nullptr;
    }
    if (h_thread_handle_) { CloseHandle(H(h_thread_handle_)); h_thread_handle_ = nullptr; }
    exited_.store(true);
}

void PtyWindows::_reader_loop() {
    char buf[4096];
    HANDLE h = H(h_out_read_);
    while (true) {
        DWORD n = 0;
        BOOL ok = ReadFile(h, buf, sizeof(buf), &n, nullptr);
        if (!ok || n == 0) break;
        std::lock_guard<std::mutex> lk(buf_mu_);
        buf_.insert(buf_.end(), buf, buf + n);
    }
    // Capture exit code before flagging exited_ so the two stay coherent.
    if (h_proc_ != nullptr) {
        DWORD code = 0;
        if (GetExitCodeProcess(H(h_proc_), &code) &&
            code != STILL_ACTIVE) {
            exit_code_ = static_cast<int>(code);
        }
    }
    exited_.store(true);
    running_.store(false);
}

bool PtyWindows::is_running() const {
    return running_.load() && !exited_.load();
}

void PtyWindows::write(const char *data, std::size_t len) {
    if (h_in_write_ == nullptr || data == nullptr || len == 0) return;
    DWORD written = 0;
    WriteFile(H(h_in_write_), data, static_cast<DWORD>(len), &written, nullptr);
}

void PtyWindows::resize(int cols, int rows) {
    if (h_pc_ == nullptr) return;
    COORD s;
    s.X = static_cast<SHORT>(cols > 0 ? cols : 1);
    s.Y = static_cast<SHORT>(rows > 0 ? rows : 1);
    ResizePseudoConsole(P(h_pc_), s);
}

void PtyWindows::drain(const DrainFn &cb) {
    std::vector<char> local;
    {
        std::lock_guard<std::mutex> lk(buf_mu_);
        if (buf_.empty()) return;
        local.swap(buf_);
    }
    cb(local.data(), local.size());
}

} // namespace godot

#endif // _WIN32
