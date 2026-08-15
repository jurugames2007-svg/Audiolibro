#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
constexpr wchar_t kTitle[] = L"VozLarga PC 1.0";
constexpr UINT WM_APP_LOG = WM_APP + 1;
constexpr UINT WM_APP_STAGE = WM_APP + 2;
constexpr UINT WM_APP_FINISHED = WM_APP + 3;
constexpr UINT TIMER_UI = 1;
constexpr double BLOCK_SECONDS = 900.0;
constexpr double OVERLAP_SECONDS = 2.0;
constexpr double STEP_SECONDS = BLOCK_SECONDS - OVERLAP_SECONDS;

enum ControlId {
    ID_INPUT = 1001, ID_BROWSE_INPUT, ID_OUTPUT, ID_BROWSE_OUTPUT,
    ID_BACKEND, ID_VAD, ID_ENHANCE, ID_STEREO, ID_CONTEXT, ID_THREADS,
    ID_START, ID_PAUSE, ID_CANCEL, ID_OPEN_FOLDER, ID_OPEN_TEXT, ID_PLAY_AUDIO,
    ID_PROGRESS, ID_STATUS, ID_ELAPSED, ID_ETA, ID_BACKEND_NOW, ID_HELP
};
enum class Stage : int { Idle, Probing, Converting, Transcribing, Consolidating, Paused, Done, Failed, Cancelled };
enum class BackendChoice : int { Automatic = 0, Vulkan = 1, Cpu = 2 };

HWND g_main = nullptr;
HWND g_input = nullptr, g_output = nullptr, g_backend = nullptr, g_vad = nullptr, g_enhance = nullptr;
HWND g_stereo = nullptr, g_context = nullptr, g_threads = nullptr;
HWND g_start = nullptr, g_pause = nullptr, g_cancel = nullptr, g_progress = nullptr, g_status = nullptr;
HWND g_elapsed = nullptr, g_eta = nullptr, g_backendNow = nullptr, g_openFolder = nullptr;
HWND g_openText = nullptr, g_playAudio = nullptr;
HFONT g_font = nullptr, g_titleFont = nullptr;
std::atomic<bool> g_running{false}, g_pauseRequested{false}, g_cancelRequested{false};
std::atomic<int> g_stage{static_cast<int>(Stage::Idle)}, g_completed{0}, g_total{0};
std::atomic<unsigned long long> g_jobStartTick{0}, g_stageStartTick{0}, g_avgChunkMs{0};
std::mutex g_childMutex;
HANDLE g_child = nullptr;
std::wstring g_moduleDir;

struct Job {
    std::wstring input;
    std::wstring output;
    BackendChoice backend = BackendChoice::Automatic;
    bool vad = false;
    bool enhance = true;
    bool stereoDiarization = false;
    std::wstring context;
    int threads = 4;
};

std::wstring Join(const std::wstring &a, const std::wstring &b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}
std::wstring Parent(const std::wstring &p) {
    size_t n = p.find_last_of(L"\\/");
    return n == std::wstring::npos ? L"." : p.substr(0, n);
}
std::wstring FileName(const std::wstring &p) {
    size_t n = p.find_last_of(L"\\/");
    return n == std::wstring::npos ? p : p.substr(n + 1);
}
std::wstring Stem(const std::wstring &p) {
    std::wstring f = FileName(p);
    size_t n = f.find_last_of(L'.');
    return n == std::wstring::npos ? f : f.substr(0, n);
}
std::wstring WithLongPrefix(const std::wstring &p) {
    if (p.rfind(L"\\\\?\\", 0) == 0) return p;
    if (p.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + p.substr(2);
    if (p.size() >= 2 && p[1] == L':') return L"\\\\?\\" + p;
    return p;
}
bool Exists(const std::wstring &p) {
    DWORD a = GetFileAttributesW(WithLongPrefix(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES;
}
bool IsDirectory(const std::wstring &p) {
    DWORD a = GetFileAttributesW(WithLongPrefix(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
uint64_t FileSize64(const std::wstring &p) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(WithLongPrefix(p).c_str(), GetFileExInfoStandard, &d)) return 0;
    return (static_cast<uint64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
}
std::string ToUtf8(const std::wstring &w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring FromUtf8(const std::string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (!n) { cp = CP_ACP; flags = 0; n = MultiByteToWideChar(cp, flags, s.data(), static_cast<int>(s.size()), nullptr, 0); }
    std::wstring w(n, 0);
    MultiByteToWideChar(cp, flags, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::wstring WinError(DWORD code = GetLastError()) {
    wchar_t *p = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t *>(&p), 0, nullptr);
    std::wstring s = p ? p : L"Error desconocido";
    if (p) LocalFree(p);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back();
    return s;
}
std::wstring FormatClock(unsigned long long ms) {
    unsigned long long sec = ms / 1000;
    unsigned long long h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
    wchar_t b[64];
    swprintf(b, 64, L"%02llu:%02llu:%02llu", h, m, s);
    return b;
}
std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, 0);
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}
void PostLog(const std::wstring &s) {
    auto *copy = new std::wstring(s);
    if (!PostMessageW(g_main, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
}
void SetStage(Stage stage, const std::wstring &text = {}) {
    g_stage = static_cast<int>(stage);
    g_stageStartTick = GetTickCount64();
    auto *copy = new std::wstring(text);
    if (!PostMessageW(g_main, WM_APP_STAGE, static_cast<WPARAM>(stage), reinterpret_cast<LPARAM>(copy))) delete copy;
}
std::wstring QuoteArg(const std::wstring &arg) {
    if (arg.find_first_of(L" \t\"") == std::wstring::npos && !arg.empty()) return arg;
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') { out.append(slashes * 2 + 1, L'\\'); out.push_back(L'\"'); slashes = 0; continue; }
        out.append(slashes, L'\\'); slashes = 0; out.push_back(c);
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

int RunProcess(const std::vector<std::wstring> &args, const std::wstring &logPath, bool cancellation = true,
               unsigned long long timeoutMs = 0) {
    if (args.empty()) return -1;
    // CreateProcess can only redirect handles that are explicitly inheritable.
    // Without SECURITY_ATTRIBUTES, FFmpeg starts with invalid stdout/stderr,
    // producing an empty log and hiding both Duration and diagnostic errors.
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE log = CreateFileW(WithLongPrefix(logPath).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        PostLog(L"No se pudo crear el registro del proceso: " + WinError());
        return -1;
    }
    HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::wstring cmd;
    for (const auto &a : args) { if (!cmd.empty()) cmd.push_back(L' '); cmd += QuoteArg(a); }
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(0);
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; si.hStdOutput = log; si.hStdError = log;
    si.hStdInput = nullInput != INVALID_HANDLE_VALUE ? nullInput : GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, g_moduleDir.c_str(), &si, &pi);
    DWORD createError = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(log);
    if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
    if (!ok) { PostLog(L"No se pudo iniciar " + FileName(args.front()) + L": " + WinError(createError)); return -1; }
    CloseHandle(pi.hThread);
    {
        std::lock_guard<std::mutex> lock(g_childMutex);
        g_child = pi.hProcess;
    }
    DWORD wait = WAIT_TIMEOUT;
    const unsigned long long processStart = GetTickCount64();
    bool timedOut = false;
    while (wait == WAIT_TIMEOUT) {
        wait = WaitForSingleObject(pi.hProcess, 200);
        if (cancellation && g_cancelRequested.load()) TerminateProcess(pi.hProcess, 1223);
        if (timeoutMs && GetTickCount64() - processStart > timeoutMs) {
            timedOut = true;
            PostLog(FileName(args.front()) + L" excedió el tiempo de seguridad y se detuvo para activar el respaldo.");
            TerminateProcess(pi.hProcess, 1460);
        }
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    if (timedOut) exitCode = 1460;
    {
        std::lock_guard<std::mutex> lock(g_childMutex);
        if (g_child == pi.hProcess) g_child = nullptr;
    }
    CloseHandle(pi.hProcess);
    return static_cast<int>(exitCode);
}
void StopChild() {
    std::lock_guard<std::mutex> lock(g_childMutex);
    if (g_child) TerminateProcess(g_child, 1223);
}

bool ReadBytes(const std::wstring &path, std::string &out) {
    out.clear();
    HANDLE f = CreateFileW(WithLongPrefix(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart < 0 || sz.QuadPart > 1024LL * 1024 * 1024) { CloseHandle(f); return false; }
    out.resize(static_cast<size_t>(sz.QuadPart));
    size_t pos = 0;
    while (pos < out.size()) {
        DWORD got = 0, want = static_cast<DWORD>(std::min<size_t>(1 << 20, out.size() - pos));
        if (!ReadFile(f, out.data() + pos, want, &got, nullptr) || !got) { CloseHandle(f); return false; }
        pos += got;
    }
    CloseHandle(f);
    return true;
}
bool WriteBytesDurable(const std::wstring &path, const std::string &data) {
    HANDLE f = CreateFileW(WithLongPrefix(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    size_t pos = 0;
    bool ok = true;
    while (pos < data.size()) {
        DWORD wrote = 0, want = static_cast<DWORD>(std::min<size_t>(1 << 20, data.size() - pos));
        if (!WriteFile(f, data.data() + pos, want, &wrote, nullptr) || !wrote) { ok = false; break; }
        pos += wrote;
    }
    if (ok) ok = !!FlushFileBuffers(f);
    CloseHandle(f);
    return ok;
}
bool EnsureDir(const std::wstring &path) {
    if (IsDirectory(path)) return true;
    int r = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}
bool RemoveTree(const std::wstring &path) {
    if (!Exists(path)) return true;
    std::wstring spec = Join(path, L"*");
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(WithLongPrefix(spec).c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            std::wstring p = Join(path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(p);
            else { SetFileAttributesW(WithLongPrefix(p).c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(WithLongPrefix(p).c_str()); }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    SetFileAttributesW(WithLongPrefix(path).c_str(), FILE_ATTRIBUTE_NORMAL);
    return !!RemoveDirectoryW(WithLongPrefix(path).c_str());
}
std::string TrimText(std::string s) {
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) s.erase(0, 3);
    auto white = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && white(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && white(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}
struct Token { size_t begin, end; std::string norm; };
std::vector<Token> Tokens(const std::string &s) {
    std::vector<Token> v; size_t i = 0;
    auto sep = [](unsigned char c) { return c <= 0x20 || c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?' || c == '-' || c == '"' || c == '(' || c == ')' || c == '[' || c == ']'; };
    while (i < s.size()) {
        while (i < s.size() && sep(static_cast<unsigned char>(s[i]))) ++i;
        size_t b = i;
        while (i < s.size() && !sep(static_cast<unsigned char>(s[i]))) ++i;
        if (i > b) {
            std::string n = s.substr(b, i - b);
            for (char &c : n) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            v.push_back({b, i, std::move(n)});
        }
    }
    return v;
}
std::string RemoveOverlap(const std::string &accumulated, std::string next) {
    next = TrimText(std::move(next));
    if (accumulated.empty() || next.empty()) return next;
    auto a = Tokens(accumulated), b = Tokens(next);
    size_t max = std::min<size_t>({50, a.size(), b.size()}), match = 0;
    for (size_t n = max; n >= 3; --n) {
        bool same = true;
        for (size_t j = 0; j < n; ++j) if (a[a.size() - n + j].norm != b[j].norm) { same = false; break; }
        if (same) { match = n; break; }
        if (n == 3) break;
    }
    if (match) next.erase(0, b[match - 1].end);
    return TrimText(std::move(next));
}
std::string FormatParagraphs(std::string text) {
    text = TrimText(std::move(text));
    std::string out; out.reserve(text.size() + text.size() / 20);
    bool pendingSpace = false, capitalize = true;
    size_t paragraphChars = 0; int sentences = 0;
    auto paragraphBreak = [&]() {
        while (!out.empty() && out.back() == ' ') out.pop_back();
        if (out.size() < 4 || out.substr(out.size() - 4) != "\r\n\r\n") out += "\r\n\r\n";
        paragraphChars = 0; sentences = 0; pendingSpace = false; capitalize = true;
    };
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char uc = static_cast<unsigned char>(text[i]);
        if (uc == ' ' || uc == '\t' || uc == '\r' || uc == '\n') { pendingSpace = !out.empty(); continue; }
        if (pendingSpace && !out.empty() && out.back() != '\n') { out.push_back(' '); ++paragraphChars; }
        pendingSpace = false;
        char c = text[i];
        if (capitalize && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        out.push_back(c); ++paragraphChars;
        if (uc >= 0x80) {
            capitalize = false; // copy remaining UTF-8 bytes without interpreting them
        } else if (c == '.' || c == '!' || c == '?') {
            ++sentences; capitalize = true;
            if (paragraphChars >= 300 && sentences >= 4) paragraphBreak();
        } else if (c != '"' && c != '\'' && c != '(' && c != '[') {
            capitalize = false;
        }
        if (paragraphChars >= 750 && (c == ',' || c == ';' || c == ':')) paragraphBreak();
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '\r' || out.back() == '\n')) out.pop_back();
    return out;
}

std::wstring WorkDirFor(const std::wstring &output) { return output + L".vozlarga-work"; }
std::wstring ChunkName(const std::wstring &work, int index, const wchar_t *suffix) {
    wchar_t b[64]; swprintf(b, 64, L"chunk-%05d%s", index + 1, suffix); return Join(work, b);
}
std::string Fingerprint(const std::wstring &path) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    GetFileAttributesExW(WithLongPrefix(path).c_str(), GetFileExInfoStandard, &d);
    uint64_t h = 1469598103934665603ULL;
    std::wstring lower = path;
    for (wchar_t &c : lower) c = static_cast<wchar_t>(towlower(c));
    for (wchar_t c : lower) { h ^= static_cast<uint16_t>(c); h *= 1099511628211ULL; }
    uint64_t size = (static_cast<uint64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
    uint64_t time = (static_cast<uint64_t>(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime;
    h ^= size; h *= 1099511628211ULL; h ^= time; h *= 1099511628211ULL;
    std::ostringstream o; o << std::hex << std::setw(16) << std::setfill('0') << h; return o.str();
}
std::string FingerprintText(const std::wstring &text) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : ToUtf8(text)) { h ^= c; h *= 1099511628211ULL; }
    std::ostringstream o; o << std::hex << std::setw(16) << std::setfill('0') << h; return o.str();
}
struct Checkpoint { std::string fingerprint; int completed = 0; int total = 0; };
bool LoadCheckpoint(const std::wstring &path, Checkpoint &c) {
    std::string s; if (!ReadBytes(path, s)) return false;
    std::istringstream in(s); std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('='); if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "fingerprint") c.fingerprint = v;
        else if (k == "completed") c.completed = atoi(v.c_str());
        else if (k == "total") c.total = atoi(v.c_str());
    }
    return !c.fingerprint.empty() && c.completed >= 0 && c.total > 0;
}
bool SaveCheckpoint(const std::wstring &path, const Checkpoint &c) {
    std::ostringstream o; o << "version=1\nfingerprint=" << c.fingerprint << "\ncompleted=" << c.completed << "\ntotal=" << c.total << "\n";
    std::wstring tmp = path + L".tmp";
    if (!WriteBytesDurable(tmp, o.str())) return false;
    return !!MoveFileExW(WithLongPrefix(tmp).c_str(), WithLongPrefix(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
double ParseDuration(const std::wstring &logPath) {
    std::string s; if (!ReadBytes(logPath, s)) return 0;
    size_t p = s.find("Duration:");
    if (p != std::string::npos) {
        int h = 0, m = 0; double sec = 0;
        if (sscanf(s.c_str() + p, "Duration: %d:%d:%lf", &h, &m, &sec) == 3)
            return h * 3600.0 + m * 60.0 + sec;
    }
    // Fallback output produced by FFmpeg's machine-readable -progress mode.
    // In older FFmpeg releases out_time_ms is expressed in microseconds.
    for (const char *key : {"out_time_us=", "out_time_ms="}) {
        p = s.rfind(key);
        if (p != std::string::npos) {
            const char *value = s.c_str() + p + strlen(key);
            unsigned long long us = _strtoui64(value, nullptr, 10);
            if (us > 0) return static_cast<double>(us) / 1000000.0;
        }
    }
    for (const char *key : {"out_time=", "time="}) {
        p = s.rfind(key);
        if (p != std::string::npos) {
            int h = 0, m = 0; double sec = 0;
            if (sscanf(s.c_str() + p + strlen(key), "%d:%d:%lf", &h, &m, &sec) == 3)
                return h * 3600.0 + m * 60.0 + sec;
        }
    }
    return 0;
}
std::wstring ProcessLogTail(const std::wstring &logPath) {
    std::string bytes;
    if (!ReadBytes(logPath, bytes) || bytes.empty()) return L"(FFmpeg no produjo detalles)";
    std::wstring text = FromUtf8(bytes);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) text.pop_back();
    constexpr size_t limit = 1800;
    if (text.size() > limit) text = L"…" + text.substr(text.size() - limit);
    return text;
}

struct WaveStats {
    bool valid = false;
    bool effectivelySilent = false;
    int channels = 0;
    int sampleRate = 0;
    double rmsDb = -120.0;
    double peakDb = -120.0;
    double clippedPercent = 0.0;
};
uint16_t ReadLe16(const std::string &s, size_t p) {
    return static_cast<uint16_t>(static_cast<unsigned char>(s[p])) |
           static_cast<uint16_t>(static_cast<unsigned char>(s[p + 1]) << 8);
}
uint32_t ReadLe32(const std::string &s, size_t p) {
    return static_cast<uint32_t>(static_cast<unsigned char>(s[p])) |
           (static_cast<uint32_t>(static_cast<unsigned char>(s[p + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(s[p + 2])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(s[p + 3])) << 24);
}
WaveStats AnalyzeWave(const std::wstring &path) {
    WaveStats stats;
    std::string data;
    if (!ReadBytes(path, data) || data.size() < 44 || data.compare(0, 4, "RIFF") || data.compare(8, 4, "WAVE")) return stats;
    size_t pos = 12, audioOffset = 0, audioBytes = 0;
    int bits = 0, codec = 0;
    while (pos + 8 <= data.size()) {
        uint32_t size = ReadLe32(data, pos + 4);
        size_t body = pos + 8;
        if (body + size > data.size()) break;
        std::string id = data.substr(pos, 4);
        if (id == "fmt " && size >= 16) {
            codec = ReadLe16(data, body);
            stats.channels = ReadLe16(data, body + 2);
            stats.sampleRate = static_cast<int>(ReadLe32(data, body + 4));
            bits = ReadLe16(data, body + 14);
        } else if (id == "data") {
            audioOffset = body; audioBytes = size; break;
        }
        pos = body + size + (size & 1U);
    }
    if ((codec != 1 && codec != 0xFFFE) || bits != 16 || stats.channels < 1 || stats.channels > 2 ||
        stats.sampleRate != 16000 || !audioOffset || audioBytes < 2 || audioOffset + audioBytes > data.size()) return stats;
    const size_t samples = audioBytes / 2;
    long double sumSquares = 0.0;
    uint32_t peak = 0; size_t clipped = 0;
    for (size_t i = 0; i < samples; ++i) {
        int16_t sample = static_cast<int16_t>(ReadLe16(data, audioOffset + i * 2));
        uint32_t magnitude = sample == INT16_MIN ? 32768U : static_cast<uint32_t>(std::abs(static_cast<int>(sample)));
        peak = std::max(peak, magnitude);
        if (magnitude >= 32700U) ++clipped;
        long double normalized = static_cast<long double>(sample) / 32768.0L;
        sumSquares += normalized * normalized;
    }
    double rms = std::sqrt(static_cast<double>(sumSquares / std::max<size_t>(1, samples)));
    stats.rmsDb = rms > 0 ? 20.0 * std::log10(rms) : -120.0;
    stats.peakDb = peak > 0 ? 20.0 * std::log10(static_cast<double>(peak) / 32768.0) : -120.0;
    stats.clippedPercent = 100.0 * static_cast<double>(clipped) / std::max<size_t>(1, samples);
    // Be conservative: never discard merely quiet speech. Only near-digital
    // silence is skipped; low recordings continue to Whisper after normalize.
    stats.effectivelySilent = stats.rmsDb < -65.0 && stats.peakDb < -50.0;
    stats.valid = true;
    return stats;
}
std::wstring FormatDecimal(double value, int precision = 1) {
    wchar_t b[64]; swprintf(b, 64, precision == 2 ? L"%.2f" : L"%.1f", value); return b;
}

bool ValidateRuntime(const Job &job, std::wstring &error) {
    struct Need { std::wstring path; const wchar_t *label; } needs[] = {
        {Join(g_moduleDir, L"tools\\ffmpeg.exe"), L"FFmpeg"},
        {Join(g_moduleDir, L"engine\\whisper-vulkan.exe"), L"motor Vulkan"},
        {Join(g_moduleDir, L"engine\\whisper-cpu.exe"), L"motor CPU"},
        {Join(g_moduleDir, L"models\\ggml-small-q5_1.bin"), L"modelo Whisper Small Q5_1"}
    };
    for (const auto &n : needs) if (!Exists(n.path)) { error = L"Falta " + std::wstring(n.label) + L":\r\n" + n.path; return false; }
    const auto model = Join(g_moduleDir, L"models\\ggml-small-q5_1.bin");
    if (FileSize64(model) != 190085487ULL) { error = L"El modelo Whisper no tiene el tamaño esperado (190085487 bytes). Vuelva a extraer el ZIP."; return false; }
    if (job.vad && job.backend != BackendChoice::Cpu) {
        auto v = Join(g_moduleDir, L"models\\ggml-silero-v6.2.0.bin");
        if (!Exists(v) || FileSize64(v) != 885098ULL) { error = L"Silero VAD está activado, pero su modelo falta o está dañado."; return false; }
    }
    return true;
}

void FinishWorker(Stage stage, const std::wstring &message) {
    SetStage(stage, message);
    g_running = false;
    PostMessageW(g_main, WM_APP_FINISHED, static_cast<WPARAM>(stage), 0);
}
void Worker(Job job) {
    g_jobStartTick = GetTickCount64(); g_stageStartTick = g_jobStartTick.load(); g_avgChunkMs = 0;
    g_completed = 0; g_total = 0;
    const std::wstring ffmpeg = Join(g_moduleDir, L"tools\\ffmpeg.exe");
    const std::wstring vkExe = Join(g_moduleDir, L"engine\\whisper-vulkan.exe");
    const std::wstring cpuExe = Join(g_moduleDir, L"engine\\whisper-cpu.exe");
    const std::wstring model = Join(g_moduleDir, L"models\\ggml-small-q5_1.bin");
    const std::wstring vadModel = Join(g_moduleDir, L"models\\ggml-silero-v6.2.0.bin");
    const std::wstring work = WorkDirFor(job.output);
    const std::wstring checkpointPath = Join(work, L"checkpoint.ini");
    const std::wstring processLog = Join(work, L"proceso.log");

    if (!EnsureDir(work)) { FinishWorker(Stage::Failed, L"No se pudo crear la carpeta de trabajo: " + WinError()); return; }
    std::wstring error;
    if (!ValidateRuntime(job, error)) { FinishWorker(Stage::Failed, error); return; }

    SetStage(Stage::Probing, L"Analizando la duración del archivo…");
    PostLog(L"Archivo: " + job.input);
    PostLog(job.enhance ? L"Preprocesamiento: reducción de ruido y normalización activadas."
                        : L"Preprocesamiento: audio original sin filtros.");
    if (job.stereoDiarization) PostLog(L"Reunión estéreo: se conservarán dos canales y se etiquetarán por canal.");
    if (!job.context.empty()) PostLog(L"Contexto y vocabulario personalizado activados.");
    int probe = RunProcess({ffmpeg, L"-hide_banner", L"-i", job.input}, processLog);
    if (g_cancelRequested) { FinishWorker(Stage::Cancelled, L"Cancelado. El punto de control válido se conserva."); return; }
    (void)probe; // FFmpeg normally returns 1 when -i is used only to inspect metadata.
    double duration = ParseDuration(processLog);
    if (!(duration > 0.0) || !std::isfinite(duration)) {
        // Some MP3 encoders and streamed containers do not publish Duration in
        // their header. Scan the first audio stream with stream copy: this is
        // much faster than decoding and gives us a machine-readable end time.
        PostLog(L"El contenedor no informó la duración; realizando un análisis rápido de la pista de audio…");
        int scan = RunProcess({ffmpeg, L"-hide_banner", L"-loglevel", L"error", L"-nostats",
                               L"-progress", L"pipe:1", L"-i", job.input, L"-map", L"0:a:0",
                               L"-vn", L"-sn", L"-dn", L"-c:a", L"copy", L"-f", L"null", L"NUL"}, processLog);
        if (g_cancelRequested) { FinishWorker(Stage::Cancelled, L"Cancelado. El punto de control válido se conserva."); return; }
        duration = ParseDuration(processLog);
        if (scan != 0 || !(duration > 0.0) || !std::isfinite(duration)) {
            PostLog(L"FFmpeg terminó con código " + std::to_wstring(scan) + L".");
            PostLog(L"Detalle de FFmpeg:\r\n" + ProcessLogTail(processLog));
            FinishWorker(Stage::Failed, L"FFmpeg no pudo leer una pista de audio válida. Revise el detalle mostrado y compruebe que el archivo no esté vacío, incompleto o protegido.");
            return;
        }
    }
    int total = std::max(1, static_cast<int>(std::ceil(std::max(0.0, duration - OVERLAP_SECONDS) / STEP_SECONDS)));
    g_total = total;
    PostLog(L"Duración: " + FormatClock(static_cast<unsigned long long>(duration * 1000)) + L". Bloques: " + std::to_wstring(total) + L".");

    Checkpoint cp{};
    cp.fingerprint = Fingerprint(job.input) + (job.vad ? "-vad1" : "-vad0") +
                     (job.enhance ? "-enh1" : "-enh0") + (job.stereoDiarization ? "-st1-" : "-st0-") +
                     FingerprintText(job.context);
    cp.total = total;
    Checkpoint old{};
    if (LoadCheckpoint(checkpointPath, old) && old.fingerprint == cp.fingerprint && old.total == total && old.completed <= total) {
        bool allPresent = true;
        for (int i = 0; i < old.completed; ++i) if (!Exists(ChunkName(work, i, L".txt"))) { allPresent = false; break; }
        if (allPresent) { cp.completed = old.completed; PostLog(L"Reanudando desde el bloque " + std::to_wstring(cp.completed + 1) + L" de " + std::to_wstring(total) + L"."); }
    }
    if (cp.completed == 0) {
        // Never mix stale chunk output with a new source.
        for (int i = 0; i < total; ++i) {
            DeleteFileW(WithLongPrefix(ChunkName(work, i, L".txt")).c_str());
            DeleteFileW(WithLongPrefix(ChunkName(work, i, L".wav")).c_str());
        }
        if (!SaveCheckpoint(checkpointPath, cp)) { FinishWorker(Stage::Failed, L"No se pudo guardar el punto de control."); return; }
    }
    g_completed = cp.completed;

    bool useCpu = job.backend == BackendChoice::Cpu;
    if (useCpu && job.vad)
        PostLog(L"Silero VAD se desactiva en el motor CPU para evitar demoras extremas en audios largos.");
    unsigned long long totalTranscribeMs = 0; int measuredChunks = 0;
    for (int i = cp.completed; i < total; ++i) {
        if (g_cancelRequested) { FinishWorker(Stage::Cancelled, L"Cancelado. Los bloques terminados se conservan para reanudar."); return; }
        if (g_pauseRequested) { FinishWorker(Stage::Paused, L"En pausa. Pulse Reanudar para continuar desde el punto de control."); return; }
        const double start = i * STEP_SECONDS;
        const double length = std::min(BLOCK_SECONDS, duration - start);
        const std::wstring wav = ChunkName(work, i, L".wav");
        const std::wstring outBase = ChunkName(work, i, L"");
        const std::wstring txt = outBase + L".txt";
        DeleteFileW(WithLongPrefix(wav).c_str()); DeleteFileW(WithLongPrefix(txt).c_str());

        SetStage(Stage::Converting, L"Preparando bloque " + std::to_wstring(i + 1) + L" de " + std::to_wstring(total) + L"…");
        wchar_t ss[64], tt[64]; swprintf(ss, 64, L"%.3f", start); swprintf(tt, 64, L"%.3f", length);
        auto makeFfmpegArgs = [&](bool filters) {
            std::vector<std::wstring> command = {ffmpeg, L"-hide_banner", L"-loglevel", L"error", L"-y",
                L"-ss", ss, L"-i", job.input, L"-t", tt, L"-vn", L"-sn", L"-dn"};
            if (filters) {
                // Deterministic preprocessing only; no additional AI model.
                // Remove rumble/hiss and gently normalize varying microphone gain.
                command.push_back(L"-af");
                command.push_back(L"highpass=f=70,lowpass=f=7600,afftdn=nf=-25,dynaudnorm=f=150:g=15");
            }
            command.insert(command.end(), {L"-ac", job.stereoDiarization ? L"2" : L"1", L"-ar", L"16000",
                                           L"-c:a", L"pcm_s16le", wav});
            return command;
        };
        int code = RunProcess(makeFfmpegArgs(job.enhance), processLog);
        if (code != 0 && job.enhance && !g_cancelRequested) {
            PostLog(L"El filtro de mejora no fue compatible con este audio; reintentando el bloque sin filtros.");
            DeleteFileW(WithLongPrefix(wav).c_str());
            code = RunProcess(makeFfmpegArgs(false), processLog);
        }
        if (g_cancelRequested) { DeleteFileW(WithLongPrefix(wav).c_str()); FinishWorker(Stage::Cancelled, L"Cancelado. Los bloques terminados se conservan para reanudar."); return; }
        if (code != 0 || !Exists(wav) || FileSize64(wav) < 44) {
            PostLog(L"Detalle de FFmpeg:\r\n" + ProcessLogTail(processLog));
            FinishWorker(Stage::Failed, L"FFmpeg falló al preparar el bloque " + std::to_wstring(i + 1) + L". Se conservaron los bloques anteriores."); return;
        }

        WaveStats quality = AnalyzeWave(wav);
        if (!quality.valid) {
            DeleteFileW(WithLongPrefix(wav).c_str());
            FinishWorker(Stage::Failed, L"El bloque WAV no superó la validación PCM interna."); return;
        }
        PostLog(L"Audio bloque " + std::to_wstring(i + 1) + L": RMS " + FormatDecimal(quality.rmsDb) +
                L" dBFS, pico " + FormatDecimal(quality.peakDb) + L" dBFS, recorte " + FormatDecimal(quality.clippedPercent, 2) + L"%.");
        if (quality.clippedPercent > 1.0)
            PostLog(L"Aviso: la grabación presenta clipping; ninguna corrección puede recuperar las muestras ya recortadas.");
        if (quality.effectivelySilent) {
            if (!WriteBytesDurable(txt, "")) { FinishWorker(Stage::Failed, L"No se pudo guardar el bloque silencioso."); return; }
            DeleteFileW(WithLongPrefix(wav).c_str());
            cp.completed = i + 1;
            if (!SaveCheckpoint(checkpointPath, cp)) { FinishWorker(Stage::Failed, L"No se pudo actualizar el punto de control."); return; }
            g_completed = cp.completed;
            PostLog(L"Bloque " + std::to_wstring(i + 1) + L" omitido: solo contiene silencio, sin enviar al motor Whisper.");
            if (g_pauseRequested && i + 1 < total) { FinishWorker(Stage::Paused, L"En pausa tras guardar el bloque " + std::to_wstring(i + 1) + L"."); return; }
            continue;
        }

        std::wstring prompt = job.context;
        if (prompt.size() > 700) prompt.resize(700); // preserve user vocabulary first
        if (i > 0) {
            std::string previousBytes;
            if (ReadBytes(ChunkName(work, i - 1, L".txt"), previousBytes)) {
                std::wstring previous = FromUtf8(TrimText(std::move(previousBytes)));
                if (previous.size() > 450) previous = previous.substr(previous.size() - 450);
                if (!previous.empty()) {
                    if (!prompt.empty()) prompt += L". ";
                    prompt += L"Contexto anterior: " + previous;
                }
            }
        }
        for (wchar_t &c : prompt) if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
        if (prompt.size() > 1200) prompt = prompt.substr(prompt.size() - 1200);

        std::vector<std::wstring> args;
        auto makeArgs = [&](bool cpu) {
            args = {cpu ? cpuExe : vkExe, L"-m", model, L"-f", wav, L"-l", L"es", L"-t", std::to_wstring(job.threads),
                    L"-otxt", L"-of", outBase, L"-np", L"-nt", L"-sns", L"-nth", L"0.55"};
            if (job.stereoDiarization) args.push_back(L"-di");
            if (!prompt.empty()) { args.push_back(L"--prompt"); args.push_back(prompt); }
            if (cpu) args.push_back(L"-ng");
            // Silero's CPU path in whisper.cpp 1.9.2 can be slower than real
            // time on long silence. Keep it on the accelerated Vulkan path;
            // CPU fallback favors guaranteed completion without VAD.
            if (job.vad && !cpu) { args.push_back(L"--vad"); args.push_back(L"-vm"); args.push_back(vadModel); }
        };
        makeArgs(useCpu);
        PostMessageW(g_main, WM_APP_STAGE, 100 + (useCpu ? 1 : 0), 0);
        SetStage(Stage::Transcribing, L"Transcribiendo bloque " + std::to_wstring(i + 1) + L" de " + std::to_wstring(total) + (useCpu ? L" con CPU…" : L" con Vulkan…"));
        unsigned long long before = GetTickCount64();
        constexpr unsigned long long VULKAN_BLOCK_TIMEOUT_MS = 20ULL * 60ULL * 1000ULL;
        code = RunProcess(args, processLog, true, useCpu ? 0 : VULKAN_BLOCK_TIMEOUT_MS);
        unsigned long long spent = GetTickCount64() - before;
        if (g_cancelRequested) { DeleteFileW(WithLongPrefix(wav).c_str()); DeleteFileW(WithLongPrefix(txt).c_str()); FinishWorker(Stage::Cancelled, L"Cancelado. Los bloques terminados se conservan para reanudar."); return; }
        if ((!useCpu) && (code != 0 || !Exists(txt))) {
            PostLog(L"Vulkan no completó el bloque. Activando automáticamente el motor CPU independiente.");
            if (job.vad) PostLog(L"Silero VAD se desactiva durante el respaldo CPU para asegurar el avance del audio largo.");
            useCpu = true; DeleteFileW(WithLongPrefix(txt).c_str()); makeArgs(true);
            PostMessageW(g_main, WM_APP_STAGE, 101, 0);
            before = GetTickCount64(); code = RunProcess(args, processLog); spent = GetTickCount64() - before;
        }
        if (code != 0 || !Exists(txt)) { FinishWorker(Stage::Failed, L"El motor CPU no pudo transcribir el bloque " + std::to_wstring(i + 1) + L". Consulte proceso.log."); return; }
        std::string check;
        if (!ReadBytes(txt, check)) { FinishWorker(Stage::Failed, L"No se pudo verificar el texto del bloque " + std::to_wstring(i + 1) + L"."); return; }
        DeleteFileW(WithLongPrefix(wav).c_str());
        cp.completed = i + 1;
        if (!SaveCheckpoint(checkpointPath, cp)) { FinishWorker(Stage::Failed, L"No se pudo actualizar el punto de control."); return; }
        g_completed = cp.completed;
        totalTranscribeMs += spent; ++measuredChunks; g_avgChunkMs = totalTranscribeMs / measuredChunks;
        PostLog(L"Bloque " + std::to_wstring(i + 1) + L" completado (" + FormatClock(spent) + L").");
        if (g_pauseRequested && i + 1 < total) { FinishWorker(Stage::Paused, L"En pausa tras guardar el bloque " + std::to_wstring(i + 1) + L"."); return; }
    }

    SetStage(Stage::Consolidating, L"Consolidando y verificando el TXT final…");
    std::string finalText;
    for (int i = 0; i < total; ++i) {
        std::string part;
        if (!ReadBytes(ChunkName(work, i, L".txt"), part)) { FinishWorker(Stage::Failed, L"Falta el texto verificado del bloque " + std::to_wstring(i + 1) + L"."); return; }
        part = RemoveOverlap(finalText, std::move(part));
        if (!part.empty()) { if (!finalText.empty()) finalText += "\r\n\r\n"; finalText += part; }
    }
    finalText = FormatParagraphs(std::move(finalText));
    if (!finalText.empty()) finalText += "\r\n";
    std::wstring tempFinal = job.output + L".tmp";
    if (!WriteBytesDurable(tempFinal, finalText)) { FinishWorker(Stage::Failed, L"No se pudo escribir el TXT temporal: " + WinError()); return; }
    std::string verify;
    if (!ReadBytes(tempFinal, verify) || verify != finalText) { FinishWorker(Stage::Failed, L"La verificación del TXT temporal falló; se conservaron los bloques."); return; }
    if (!MoveFileExW(WithLongPrefix(tempFinal).c_str(), WithLongPrefix(job.output).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        FinishWorker(Stage::Failed, L"No se pudo publicar el TXT final de forma atómica: " + WinError()); return;
    }
    verify.clear();
    if (!ReadBytes(job.output, verify) || verify != finalText) { FinishWorker(Stage::Failed, L"El TXT final no superó la verificación. Los bloques se conservaron."); return; }
    if (!RemoveTree(work)) PostLog(L"Aviso: el TXT es válido, pero no se pudo borrar toda la carpeta temporal.");
    FinishWorker(Stage::Done, L"Transcripción terminada y verificada:\r\n" + job.output);
}

void AppendLog(const std::wstring &line) {
    int n = GetWindowTextLengthW(g_status);
    SendMessageW(g_status, EM_SETSEL, n, n);
    std::wstring s = (n ? L"\r\n" : L"") + line;
    SendMessageW(g_status, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(s.c_str()));
    SendMessageW(g_status, EM_SCROLLCARET, 0, 0);
}
std::wstring BrowseOpen(HWND owner) {
    wchar_t file[32768]{};
    OPENFILENAMEW o{}; o.lStructSize = sizeof(o); o.hwndOwner = owner; o.lpstrFile = file; o.nMaxFile = 32768;
    o.lpstrFilter = L"Audio y vídeo compatibles\0*.mp3;*.m4a;*.aac;*.wav;*.flac;*.ogg;*.opus;*.mp4;*.mkv;*.webm;*.mov;*.avi;*.wma;*.m4v\0Todos los archivos\0*.*\0";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&o) ? file : L"";
}
std::wstring BrowseSave(HWND owner, const std::wstring &suggestion) {
    wchar_t file[32768]{}; wcsncpy(file, suggestion.c_str(), 32767);
    OPENFILENAMEW o{}; o.lStructSize = sizeof(o); o.hwndOwner = owner; o.lpstrFile = file; o.nMaxFile = 32768;
    o.lpstrFilter = L"Texto UTF-8\0*.txt\0"; o.lpstrDefExt = L"txt";
    o.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
    return GetSaveFileNameW(&o) ? file : L"";
}
void SetControlsRunning(bool running) {
    EnableWindow(g_input, !running); EnableWindow(g_output, !running); EnableWindow(g_backend, !running);
    EnableWindow(g_vad, !running); EnableWindow(g_enhance, !running); EnableWindow(g_stereo, !running);
    EnableWindow(g_context, !running); EnableWindow(g_threads, !running);
    EnableWindow(GetDlgItem(g_main, ID_BROWSE_INPUT), !running); EnableWindow(GetDlgItem(g_main, ID_BROWSE_OUTPUT), !running);
    EnableWindow(g_start, !running); EnableWindow(g_pause, running); EnableWindow(g_cancel, running);
    SetWindowTextW(g_pause, g_pauseRequested ? L"Pausa solicitada" : L"Pausar");
}
void StartJob() {
    if (g_running) return;
    Job j; j.input = GetText(g_input); j.output = GetText(g_output);
    j.backend = static_cast<BackendChoice>(SendMessageW(g_backend, CB_GETCURSEL, 0, 0));
    j.vad = SendMessageW(g_vad, BM_GETCHECK, 0, 0) == BST_CHECKED;
    j.enhance = SendMessageW(g_enhance, BM_GETCHECK, 0, 0) == BST_CHECKED;
    j.stereoDiarization = SendMessageW(g_stereo, BM_GETCHECK, 0, 0) == BST_CHECKED;
    j.context = GetText(g_context);
    j.threads = _wtoi(GetText(g_threads).c_str()); j.threads = std::clamp(j.threads, 1, 64);
    if (j.input.empty() || !Exists(j.input)) { MessageBoxW(g_main, L"Seleccione un archivo de audio o vídeo válido.", kTitle, MB_OK | MB_ICONWARNING); return; }
    if (j.output.empty()) { MessageBoxW(g_main, L"Seleccione el TXT de salida.", kTitle, MB_OK | MB_ICONWARNING); return; }
    if (_wcsicmp(j.input.c_str(), j.output.c_str()) == 0) { MessageBoxW(g_main, L"La salida no puede ser el mismo archivo que la entrada.", kTitle, MB_OK | MB_ICONWARNING); return; }
    std::wstring runtimeError;
    if (!ValidateRuntime(j, runtimeError)) { MessageBoxW(g_main, runtimeError.c_str(), kTitle, MB_OK | MB_ICONERROR); return; }
    if (Exists(j.output)) {
        if (MessageBoxW(g_main, L"El TXT de salida ya existe. ¿Desea reemplazarlo al finalizar?", kTitle, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }
    EnsureDir(Parent(j.output));
    SetWindowTextW(g_status, L"");
    g_pauseRequested = false; g_cancelRequested = false; g_running = true;
    g_stage = static_cast<int>(Stage::Probing); g_completed = 0; g_total = 0;
    SendMessageW(g_progress, PBM_SETSTATE, PBST_NORMAL, 0);
    SendMessageW(g_progress, PBM_SETPOS, 0, 0); SetControlsRunning(true);
    SetWindowTextW(g_start, L"Iniciar");
    std::thread(Worker, std::move(j)).detach();
}

void Layout(HWND w, int width, int height) {
    const int margin = 24, labelW = 96, buttonW = 112, rowH = 30, gap = 10;
    int y = 70;
    auto placeRow = [&](HWND edit, int buttonId) {
        MoveWindow(edit, margin + labelW, y, width - margin * 2 - labelW - buttonW - gap, rowH, TRUE);
        MoveWindow(GetDlgItem(w, buttonId), width - margin - buttonW, y, buttonW, rowH, TRUE); y += 46;
    };
    MoveWindow(GetDlgItem(w, 9001), margin, y + 5, labelW, 20, TRUE); placeRow(g_input, ID_BROWSE_INPUT);
    MoveWindow(GetDlgItem(w, 9002), margin, y + 5, labelW, 20, TRUE); placeRow(g_output, ID_BROWSE_OUTPUT);

    MoveWindow(GetDlgItem(w, 9003), margin, y + 5, labelW, 20, TRUE);
    MoveWindow(g_backend, margin + labelW, y, 270, 200, TRUE);
    MoveWindow(GetDlgItem(w, 9004), width - margin - 150, y + 5, 68, 20, TRUE);
    MoveWindow(g_threads, width - margin - 76, y, 76, rowH, TRUE); y += 38;

    MoveWindow(g_enhance, margin + labelW, y, 210, 24, TRUE);
    MoveWindow(g_vad, margin + labelW + 220, y, 210, 24, TRUE);
    MoveWindow(g_stereo, margin + labelW + 440, y, 250, 24, TRUE); y += 36;

    MoveWindow(GetDlgItem(w, 9005), margin, y + 5, labelW, 20, TRUE);
    MoveWindow(g_context, margin + labelW, y, width - margin * 2 - labelW, rowH, TRUE); y += 44;

    int bw = 104;
    MoveWindow(g_start, margin, y, bw, 34, TRUE);
    MoveWindow(g_pause, margin + bw + gap, y, bw, 34, TRUE);
    MoveWindow(g_cancel, margin + (bw + gap) * 2, y, bw, 34, TRUE);
    MoveWindow(g_playAudio, margin + (bw + gap) * 3, y, 126, 34, TRUE);
    MoveWindow(g_openText, margin + (bw + gap) * 3 + 136, y, 112, 34, TRUE);
    MoveWindow(g_openFolder, width - margin - 138, y, 138, 34, TRUE); y += 48;

    MoveWindow(g_progress, margin, y, width - margin * 2, 22, TRUE); y += 34;
    int infoW = (width - margin * 2) / 3;
    MoveWindow(g_elapsed, margin, y, infoW, 22, TRUE); MoveWindow(g_eta, margin + infoW, y, infoW, 22, TRUE);
    MoveWindow(g_backendNow, margin + infoW * 2, y, infoW, 22, TRUE); y += 30;
    MoveWindow(g_status, margin, y, width - margin * 2, std::max(100, height - y - margin), TRUE);
}
HWND Make(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int id, DWORD ex = 0) {
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE); return h;
}
void UpdateUiTimer() {
    if (!g_running) return;
    unsigned long long now = GetTickCount64(), elapsed = now - g_jobStartTick.load();
    SetWindowTextW(g_elapsed, (L"Transcurrido: " + FormatClock(elapsed)).c_str());
    int total = g_total.load(), done = g_completed.load();
    Stage st = static_cast<Stage>(g_stage.load());
    double fraction = total > 0 ? static_cast<double>(done) / total : 0.01;
    if (total > 0 && st == Stage::Converting) fraction = (done + 0.05) / total;
    if (total > 0 && st == Stage::Transcribing) {
        unsigned long long avg = g_avgChunkMs.load(); if (!avg) avg = 120000;
        double current = std::min(0.95, static_cast<double>(now - g_stageStartTick.load()) / std::max<unsigned long long>(1, avg));
        fraction = (done + 0.08 + current * 0.88) / total;
    }
    if (st == Stage::Consolidating) fraction = 0.99;
    fraction = std::clamp(fraction, 0.0, 0.995);
    SendMessageW(g_progress, PBM_SETPOS, static_cast<int>(fraction * 1000), 0);
    std::wstring eta = L"ETA: calculando…";
    if (done > 0 && total > done) {
        unsigned long long avg = g_avgChunkMs.load();
        if (avg) eta = L"ETA: " + FormatClock(avg * static_cast<unsigned long long>(total - done));
    }
    SetWindowTextW(g_eta, eta.c_str());
}

LRESULT CALLBACK WndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_main = w;
        NONCLIENTMETRICSW ncm{sizeof(ncm)}; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
        LOGFONTW lf = ncm.lfMessageFont; lf.lfHeight = -26; lf.lfWeight = FW_SEMIBOLD;
        g_titleFont = CreateFontIndirectW(&lf);
        HWND title = Make(w, L"STATIC", L"VozLarga PC", SS_LEFT, 9000); SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(g_titleFont), TRUE);
        SetWindowPos(title, nullptr, 24, 20, 300, 36, SWP_NOZORDER);
        Make(w, L"STATIC", L"Entrada", SS_LEFT, 9001); g_input = Make(w, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, ID_INPUT, WS_EX_CLIENTEDGE);
        Make(w, L"BUTTON", L"Examinar…", BS_PUSHBUTTON, ID_BROWSE_INPUT);
        Make(w, L"STATIC", L"Salida TXT", SS_LEFT, 9002); g_output = Make(w, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, ID_OUTPUT, WS_EX_CLIENTEDGE);
        Make(w, L"BUTTON", L"Examinar…", BS_PUSHBUTTON, ID_BROWSE_OUTPUT);
        Make(w, L"STATIC", L"Motor", SS_LEFT, 9003); g_backend = Make(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, ID_BACKEND);
        SendMessageW(g_backend, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Automático (Vulkan → CPU)"));
        SendMessageW(g_backend, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Vulkan (con respaldo CPU)"));
        SendMessageW(g_backend, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Solo CPU")); SendMessageW(g_backend, CB_SETCURSEL, 0, 0);
        g_enhance = Make(w, L"BUTTON", L"Mejorar voz/ruido", BS_AUTOCHECKBOX, ID_ENHANCE); SendMessageW(g_enhance, BM_SETCHECK, BST_CHECKED, 0);
        g_vad = Make(w, L"BUTTON", L"Silero VAD (solo Vulkan)", BS_AUTOCHECKBOX, ID_VAD); SendMessageW(g_vad, BM_SETCHECK, BST_UNCHECKED, 0);
        g_stereo = Make(w, L"BUTTON", L"Reunión estéreo (canales)", BS_AUTOCHECKBOX, ID_STEREO); SendMessageW(g_stereo, BM_SETCHECK, BST_UNCHECKED, 0);
        Make(w, L"STATIC", L"Hilos CPU", SS_LEFT, 9004);
        SYSTEM_INFO si{}; GetSystemInfo(&si); int threads = std::max<DWORD>(1, si.dwNumberOfProcessors > 2 ? si.dwNumberOfProcessors - 1 : si.dwNumberOfProcessors);
        g_threads = Make(w, L"EDIT", std::to_wstring(threads).c_str(), WS_BORDER | ES_NUMBER | ES_CENTER, ID_THREADS, WS_EX_CLIENTEDGE);
        Make(w, L"STATIC", L"Contexto", SS_LEFT, 9005);
        g_context = Make(w, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, ID_CONTEXT, WS_EX_CLIENTEDGE);
        SendMessageW(g_context, EM_SETLIMITTEXT, 1000, 0);
        SendMessageW(g_context, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Nombres propios, empresa, tema, términos técnicos o dialecto chileno…"));
        g_start = Make(w, L"BUTTON", L"Iniciar", BS_DEFPUSHBUTTON, ID_START);
        g_pause = Make(w, L"BUTTON", L"Pausar", BS_PUSHBUTTON, ID_PAUSE); EnableWindow(g_pause, FALSE);
        g_cancel = Make(w, L"BUTTON", L"Cancelar", BS_PUSHBUTTON, ID_CANCEL); EnableWindow(g_cancel, FALSE);
        g_playAudio = Make(w, L"BUTTON", L"Reproducir audio", BS_PUSHBUTTON, ID_PLAY_AUDIO);
        g_openText = Make(w, L"BUTTON", L"Abrir TXT", BS_PUSHBUTTON, ID_OPEN_TEXT);
        g_openFolder = Make(w, L"BUTTON", L"Abrir carpeta", BS_PUSHBUTTON, ID_OPEN_FOLDER);
        g_progress = Make(w, PROGRESS_CLASSW, L"", PBS_SMOOTH, ID_PROGRESS); SendMessageW(g_progress, PBM_SETRANGE32, 0, 1000);
        SendMessageW(g_progress, PBM_SETSTATE, PBST_NORMAL, 0);
        g_elapsed = Make(w, L"STATIC", L"Transcurrido: 00:00:00", SS_LEFT, ID_ELAPSED);
        g_eta = Make(w, L"STATIC", L"ETA: —", SS_CENTER, ID_ETA);
        g_backendNow = Make(w, L"STATIC", L"Motor actual: —", SS_RIGHT, ID_BACKEND_NOW);
        g_status = Make(w, L"EDIT", L"Listo. Todo el procesamiento se realiza localmente.", WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, ID_STATUS, WS_EX_CLIENTEDGE);
        SetTimer(w, TIMER_UI, 500, nullptr);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto *info = reinterpret_cast<MINMAXINFO *>(lp);
        info->ptMinTrackSize.x = 860; info->ptMinTrackSize.y = 680; return 0;
    }
    case WM_SIZE: Layout(w, LOWORD(lp), HIWORD(lp)); return 0;
    case WM_TIMER: if (wp == TIMER_UI) UpdateUiTimer(); return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BROWSE_INPUT: {
            std::wstring p = BrowseOpen(w); if (!p.empty()) { SetWindowTextW(g_input, p.c_str()); if (GetText(g_output).empty()) { std::wstring s = Join(Parent(p), Stem(p) + L"-transcripcion.txt"); SetWindowTextW(g_output, s.c_str()); } }
            return 0;
        }
        case ID_BROWSE_OUTPUT: { std::wstring p = BrowseSave(w, GetText(g_output)); if (!p.empty()) SetWindowTextW(g_output, p.c_str()); return 0; }
        case ID_START: StartJob(); return 0;
        case ID_PAUSE:
            if (g_running && !g_pauseRequested.exchange(true)) { SetWindowTextW(g_pause, L"Pausa solicitada"); AppendLog(L"La pausa se aplicará al terminar el bloque actual y guardar el punto de control."); }
            return 0;
        case ID_CANCEL:
            if (g_running && MessageBoxW(w, L"¿Cancelar ahora? Los bloques ya terminados se conservarán para poder reanudar.", kTitle, MB_YESNO | MB_ICONQUESTION) == IDYES) { g_cancelRequested = true; StopChild(); }
            return 0;
        case ID_PLAY_AUDIO: {
            std::wstring p = GetText(g_input);
            if (Exists(p)) ShellExecuteW(w, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            else MessageBoxW(w, L"Seleccione primero un audio válido.", kTitle, MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        case ID_OPEN_TEXT: {
            std::wstring p = GetText(g_output);
            if (Exists(p)) ShellExecuteW(w, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            else MessageBoxW(w, L"El TXT todavía no existe. Termine o reanude la transcripción.", kTitle, MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        case ID_OPEN_FOLDER: { std::wstring p = GetText(g_output); p = p.empty() ? g_moduleDir : Parent(p); ShellExecuteW(w, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0; }
        }
        break;
    case WM_APP_LOG: {
        auto *s = reinterpret_cast<std::wstring *>(lp); if (s) { AppendLog(*s); delete s; } return 0;
    }
    case WM_APP_STAGE:
        if (wp == 100 || wp == 101) { SetWindowTextW(g_backendNow, wp == 100 ? L"Motor actual: Vulkan" : L"Motor actual: CPU"); return 0; }
        else { auto *s = reinterpret_cast<std::wstring *>(lp); if (s) { if (!s->empty()) AppendLog(*s); delete s; } return 0; }
    case WM_APP_FINISHED: {
        Stage st = static_cast<Stage>(wp); SetControlsRunning(false);
        if (st == Stage::Done) { SendMessageW(g_progress, PBM_SETPOS, 1000, 0); SetWindowTextW(g_eta, L"ETA: 00:00:00"); MessageBeep(MB_ICONASTERISK); }
        else if (st == Stage::Paused) { SetWindowTextW(g_start, L"Reanudar"); EnableWindow(g_start, TRUE); }
        else if (st == Stage::Failed) { SendMessageW(g_progress, PBM_SETSTATE, PBST_ERROR, 0); MessageBoxW(w, L"La operación no terminó. Revise el estado para ver los detalles. Los bloques válidos no se han borrado.", kTitle, MB_OK | MB_ICONERROR); }
        else if (st == Stage::Cancelled) SetWindowTextW(g_start, L"Reanudar");
        return 0;
    }
    case WM_CLOSE:
        if (g_running) {
            if (MessageBoxW(w, L"Hay un trabajo en curso. ¿Cancelar y cerrar? Se conservará el último punto de control.", kTitle, MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
            g_cancelRequested = true; StopChild();
            ShowWindow(w, SW_HIDE);
            for (int i = 0; i < 100 && g_running; ++i) { Sleep(50); MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m); }
        }
        DestroyWindow(w); return 0;
    case WM_DESTROY:
        KillTimer(w, TIMER_UI); if (g_font) DeleteObject(g_font); if (g_titleFont) DeleteObject(g_titleFont); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    wchar_t path[32768]{}; DWORD n = GetModuleFileNameW(nullptr, path, 32768);
    g_moduleDir = Parent(std::wstring(path, n));
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW wc{sizeof(wc)}; wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = WndProc; wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1)); wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); wc.lpszClassName = L"VozLargaWindow";
    if (!RegisterClassExW(&wc)) return 1;
    HWND w = CreateWindowExW(0, wc.lpszClassName, kTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 940, 740, nullptr, nullptr, instance, nullptr);
    if (!w) return 2;
    RECT r{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0);
    int ww = 940, wh = 740; SetWindowPos(w, nullptr, r.left + (r.right-r.left-ww)/2, r.top + (r.bottom-r.top-wh)/2, ww, wh, SWP_NOZORDER);
    ShowWindow(w, show); UpdateWindow(w);
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return static_cast<int>(msg.wParam);
}
