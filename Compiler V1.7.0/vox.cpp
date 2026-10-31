#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <climits>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#include "lexer.h"
#include "token.h"
#include "Parser.h"
#include "AstNodes.h"
#include "exception.h"
#include "variables.h"

// ============================================================
// Windows API 声明（前置）
// ============================================================
#ifdef _WIN32
extern "C" __declspec(dllimport) void* __stdcall GetStdHandle(unsigned long);
extern "C" __declspec(dllimport) int   __stdcall SetConsoleMode(void*, unsigned long);
extern "C" __declspec(dllimport) int   __stdcall GetConsoleMode(void*, unsigned long*);
extern "C" __declspec(dllimport) int   __stdcall SetConsoleOutputCP(unsigned int);
extern "C" __declspec(dllimport) int   __stdcall SetConsoleCP(unsigned int);

extern "C" __declspec(dllimport) int __stdcall MultiByteToWideChar(
    unsigned int CodePage, unsigned long dwFlags, const char* lpMultiByteStr,
    int cbMultiByte, wchar_t* lpWideCharStr, int cchWideChar);
extern "C" __declspec(dllimport) int __stdcall WideCharToMultiByte(
    unsigned int CodePage, unsigned long dwFlags, const wchar_t* lpWideCharStr,
    int cchWideChar, char* lpMultiByteStr, int cbMultiByte,
    const char* lpDefaultChar, int* lpUsedDefaultChar);

extern "C" unsigned long __stdcall vox_GetModuleFileNameA(
    void* hModule, char* lpFilename, unsigned long nSize)
    __asm__("GetModuleFileNameA");

#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
#ifndef CP_ACP
#define CP_ACP 0
#endif
#endif

// ============================================================
// 编码辅助（前置，exeDir 依赖 ansiToUtf8）
// ============================================================
#ifdef _WIN32
static std::string ansiToUtf8(const std::string& s) {
    if (s.empty()) return s;
    int wlen = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return s;
    std::wstring wbuf(wlen, 0);
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, &wbuf[0], wlen);
    if (!wbuf.empty() && wbuf.back() == L'\0') wbuf.pop_back();

    int u8len = WideCharToMultiByte(CP_UTF8, 0, wbuf.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return s;
    std::string ubuf(u8len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wbuf.c_str(), -1, &ubuf[0], u8len, nullptr, nullptr);
    if (!ubuf.empty() && ubuf.back() == '\0') ubuf.pop_back();
    return ubuf;
}

static std::string utf8_to_ansi(const std::string& utf8) {
    if (utf8.empty()) return "";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
    if (wlen == 0) return "";
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], wlen);
    if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();

    int alen = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (alen == 0) return "";
    std::string astr(alen, 0);
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &astr[0], alen, NULL, NULL);
    if (!astr.empty() && astr.back() == '\0') astr.pop_back();
    return astr;
}
#else
static std::string ansiToUtf8(const std::string& s) { return s; }
static std::string utf8_to_ansi(const std::string& s) { return s; }
#endif

// ============================================================
// 进程创建结构体 + API（前置，run_process / compileToExe 依赖）
// ============================================================
#ifdef _WIN32
struct STARTUPINFOW;
struct PROCESS_INFORMATION;

extern "C" __declspec(dllimport) int __stdcall CreateProcessW(
    const wchar_t* lpApplicationName, wchar_t* lpCommandLine,
    void* lpProcessAttributes, void* lpThreadAttributes,
    int bInheritHandles, unsigned long dwCreationFlags,
    void* lpEnvironment, const wchar_t* lpCurrentDirectory,
    STARTUPINFOW* lpStartupInfo, PROCESS_INFORMATION* lpProcessInformation);

extern "C" __declspec(dllimport) unsigned long __stdcall WaitForSingleObject(
    void* hHandle, unsigned long dwMilliseconds);

extern "C" __declspec(dllimport) int __stdcall GetExitCodeProcess(
    void* hProcess, unsigned long* lpExitCode);

extern "C" __declspec(dllimport) int __stdcall CloseHandle(void* hObject);

struct STARTUPINFOW {
    unsigned long cb;
    wchar_t* lpReserved;
    wchar_t* lpDesktop;
    wchar_t* lpTitle;
    unsigned long dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    unsigned short wShowWindow, cbReserved2;
    unsigned char* lpReserved2;
    void* hStdInput, *hStdOutput, *hStdError;
};
struct PROCESS_INFORMATION {
    void* hProcess;
    void* hThread;
    unsigned long dwProcessId;
    unsigned long dwThreadId;
};
#endif

// ============================================================
// 颜色开关
// ============================================================
static bool g_color_enabled = false;

static bool init_console_color() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    void* hOut = GetStdHandle((unsigned long)-11);
    if (!hOut || hOut == (void*)-1) return false;
    unsigned long mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return false;
    if (!SetConsoleMode(hOut, mode | 0x0004)) return false;
    return true;
#else
    return true;
#endif
}

// ============================================================
// 全局
// ============================================================
namespace fs = std::filesystem;

static const char* VOX_VERSION = "1.7.0";
static std::vector<std::string> g_extraSearchDirs;

// ============================================================
// 路径
// ============================================================
static fs::path exeDir(){
#ifdef _WIN32
    char buf[MAX_PATH];
    unsigned long n = vox_GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH){
        return fs::u8path(ansiToUtf8(std::string(buf, n))).parent_path();
    }
    return fs::current_path();
#else
    std::error_code ec;
    auto p = fs::canonical("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
    return fs::current_path();
#endif
}

static fs::path configPath(){
    return exeDir() / "vox.config";
}

static fs::path readMinGWRoot(){
    std::ifstream in(configPath());
    if (!in) return {};

    std::string line;
    while (std::getline(in, line)){
        if (line.rfind("mingw=", 0) == 0){
            std::string p = line.substr(6);
            while (!p.empty() && (p.front() == ' ' || p.front() == '"')) p.erase(p.begin());
            while (!p.empty() && (p.back()  == ' ' || p.back()  == '"')) p.pop_back();
            if (!p.empty()) return fs::u8path(p);
        }
    }
    return {};
}

static bool writeMinGWRoot(const fs::path& root){
    std::ofstream out(configPath(), std::ios::trunc);
    if (!out) return false;
    out << "mingw=" << root.u8string() << "\n";
    return true;
}

static fs::path gxxPath(){
    fs::path root = readMinGWRoot();
    if (!root.empty()) {
#ifdef _WIN32
        fs::path p = root / "bin" / "g++.exe";
        if (fs::exists(p)) return p;
        p = root / "g++.exe";
        if (fs::exists(p)) return p;
#else
        fs::path p = root / "bin" / "g++";
        if (fs::exists(p)) return p;
        p = root / "g++";
        if (fs::exists(p)) return p;
#endif
    }

    fs::path localRoot = exeDir() / "mingw64";
#ifdef _WIN32
    fs::path p = localRoot / "bin" / "g++.exe";
    if (fs::exists(p)) return p;
    p = localRoot / "g++.exe";
    if (fs::exists(p)) return p;
#else
    fs::path p = localRoot / "bin" / "g++";
    if (fs::exists(p)) return p;
    p = localRoot / "g++";
    if (fs::exists(p)) return p;
#endif

    return {};
}

// ============================================================
// 工具
// ============================================================
static std::string readFileText(const fs::path& p){
    std::ifstream in(p, std::ios::in | std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static int run_process(const std::string& cmd) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, NULL, 0);
    if (wlen <= 0) return -1;
    std::wstring wcmd(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, &wcmd[0], wlen);

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    if (CreateProcessW(NULL, &wcmd[0], NULL, NULL, 0, 0, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, (unsigned long)-1);
        unsigned long exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return (int)exitCode;
    }
    return -1;
#else
    return std::system(cmd.c_str());
#endif
}

// ============================================================
// compileToExe
// ============================================================
static bool compileToExe(const std::string& generated,
                         const fs::path& outExe,
                         const fs::path& includeDir,
                         const std::vector<std::string>& searchDirs,
                         std::string& errOut,
                         bool verbose,
                         bool copyDlls = false,
                         bool guiMode = false,
                         bool optimize = false) {

    std::string hashData = generated;
    hashData += (optimize ? "|opt=O2" : "|opt=O0");
    hashData += (guiMode ? "|gui" : "|console");
    hashData += "|v1.6.1";

    std::hash<std::string> hasher;
    size_t codeHash = hasher(hashData);

    fs::path cacheDir = fs::temp_directory_path() / "vox_cache";
    fs::create_directories(cacheDir);
    fs::path cacheExe = cacheDir / (std::to_string(codeHash) + ".exe");

    if (fs::exists(cacheExe)) {
        std::error_code ec;
        fs::copy_file(cacheExe, outExe, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            if (verbose) std::cout << "[Cache hit] Using cached binary for Hash " << codeHash << std::endl;
            return true;
        }
    }

    std::srand(static_cast<unsigned>(std::time(nullptr)));
    fs::path tmpDir = fs::temp_directory_path();
    fs::path tmpCpp = tmpDir / ("vox_" + std::to_string(std::rand()) + ".cpp");

    {
        std::ofstream out(tmpCpp);
        if (!out){
            errOut = "Cannot write " + tmpCpp.u8string();
            return false;
        }
        out << generated;
    }

    fs::path gxx = gxxPath();
    if (gxx.empty() || !fs::exists(gxx)){
        errOut =
            "Error: g++ compiler not found.\n\n"
            "Vox looks for the compiler in this order:\n"
            "  1. The path configured in: " + configPath().u8string() + "\n"
            "  2. Local folder: " + (exeDir() / "mingw64").u8string() + "\\bin\\g++.exe\n\n"
            "How to fix:\n"
            "  - Put the MinGW folder next to vox.exe and rename it to 'mingw64'\n"
            "    (so that " + (exeDir() / "mingw64" / "bin" / "g++.exe").u8string() + " exists)\n"
            "  - OR run: vox cpp <path-to-mingw-root>\n"
            "    (e.g., vox cpp \"D:\\MinGW\")";
        std::error_code ec;
        fs::remove(tmpCpp, ec);
        return false;
    }

    auto findCodeDir = [](const fs::path& base) -> fs::path {
        fs::path candidate = base / "Code";
        if (fs::exists(candidate / "vox_runtime.h")){
            return candidate;
        }
        return fs::path();
    };

    fs::path runtimeDir;
    std::vector<std::string> tried;

    for (const auto& d : searchDirs){
        fs::path r = findCodeDir(fs::u8path(d));
        if (!r.empty()){
            runtimeDir = r;
            break;
        }
        tried.push_back((fs::u8path(d) / "Code").u8string());
    }

    if (runtimeDir.empty()){
        fs::path r = findCodeDir(exeDir());
        if (!r.empty()) runtimeDir = r;
        else tried.push_back((exeDir() / "Code").u8string());
    }
    if (runtimeDir.empty()){
        fs::path r = findCodeDir(fs::current_path());
        if (!r.empty()) runtimeDir = r;
        else tried.push_back((fs::current_path() / "Code").u8string());
    }

    if (runtimeDir.empty()){
        std::string msg = "Cannot find 'Code/' directory (vox_runtime.h not found).\n  Tried:\n";
        for (const auto& t : tried){
            msg += "    " + t + "\n";
        }
        errOut = msg;
        std::error_code ec;
        fs::remove(tmpCpp, ec);
        return false;
    }

    fs::path sdl3Root = runtimeDir / "vendor" / "SDL3";
    fs::path vendorDir = runtimeDir / "vendor";
    fs::path sdlInc, sdlLib, sdlBin;

    auto trySet = [](fs::path& out, const fs::path& p) -> bool {
        std::error_code ec;
        if (fs::exists(p, ec) && fs::is_directory(p, ec)){
            out = p;
            return true;
        }
        return false;
    };

    fs::path ffmpegRoot = vendorDir / "ffmpeg";
    fs::path ffInc, ffLib, ffBin;

    for (const auto& cand : {
        ffmpegRoot / "include",
        runtimeDir / "vendor" / "ffmpeg" / "include"
    }){
        std::error_code ec;
        if (fs::exists(cand / "libavformat" / "avformat.h", ec)){
            ffInc = cand;
            break;
        }
    }
    for (const auto& cand : {
        ffmpegRoot / "lib",
        runtimeDir / "vendor" / "ffmpeg" / "lib"
    }){
        if (trySet(ffLib, cand)) break;
    }
    for (const auto& cand : {
        ffmpegRoot / "bin",
        runtimeDir / "vendor" / "ffmpeg" / "bin"
    }){
        if (trySet(ffBin, cand)) break;
    }

    if (verbose){
        std::cout << "FFmpeg include: " << ffInc.u8string() << "\n";
        std::cout << "FFmpeg lib    : " << ffLib.u8string() << "\n";
        std::cout << "FFmpeg bin    : " << ffBin.u8string() << "\n";
    }

    for (const auto& cand : {
        sdl3Root / "x86_64-w64-mingw32" / "include",
        sdl3Root / "i686-w64-mingw32"   / "include",
        sdl3Root / "install"            / "include",
        sdl3Root / "include"
    }){
        std::error_code ec;
        if (fs::exists(cand / "SDL3" / "SDL.h", ec)){
            sdlInc = cand;
            break;
        }
    }

    for (const auto& cand : {
        sdl3Root / "install"            / "lib",
        sdl3Root / "x86_64-w64-mingw32" / "lib",
        sdl3Root / "i686-w64-mingw32"   / "lib",
        sdl3Root / "lib"
    }){
        if (trySet(sdlLib, cand)) break;
    }

    for (const auto& cand : {
        sdl3Root / "install"            / "bin",
        sdl3Root / "x86_64-w64-mingw32" / "bin",
        sdl3Root / "i686-w64-mingw32"   / "bin",
        sdl3Root / "bin"
    }){
        if (trySet(sdlBin, cand)) break;
    }

    if (sdlInc.empty()){
        errOut = "Cannot find SDL3 headers under: " + sdl3Root.u8string()
               + "\n  (expected SDL3/SDL.h in one of x86_64-w64-mingw32/include, "
                 "i686-w64-mingw32/include, install/include, include)";
        std::error_code ec;
        fs::remove(tmpCpp, ec);
        return false;
    }
    if (verbose){
        std::cout << "SDL3 include: " << sdlInc.u8string() << "\n";
        std::cout << "SDL3 lib    : " << sdlLib.u8string() << "\n";
        std::cout << "SDL3 bin    : " << sdlBin.u8string() << "\n";
    }

    std::string opFalg = optimize ? "-O2" : "-O0 -pipe";

    std::string cmd =
        "\"" + gxx.u8string() + "\""
        + " -std=c++17 " + opFalg
        + " -I \"" + includeDir.u8string() + "\""
        + " -I \"" + runtimeDir.u8string() + "\"";

    {
        std::set<std::string> already;
        already.insert(includeDir.u8string());
        already.insert(runtimeDir.u8string());
        for (const auto& d : searchDirs){
            if (!d.empty() && already.insert(d).second){
                cmd += " -I \"" + d + "\"";
            }
        }
    }

    cmd +=
        " -I \"" + sdlInc.u8string() + "\""
        + " -I \"" + vendorDir.u8string() + "\"";

    if (!ffInc.empty()){
        cmd += " -I \"" + ffInc.u8string() + "\"";
    }

    cmd += " \"" + tmpCpp.u8string() + "\"";

    // ★ ziplib 依赖 miniz.c 作为独立编译单元
    //   检测生成代码里是否 #include 了 ziplib.cpp，有的话加入 miniz.c
    if (generated.find("ziplib.cpp") != std::string::npos){
        fs::path minizC = runtimeDir / "vendor" / "miniz" / "miniz.c";
        if (fs::exists(minizC)){
            cmd += " \"" + minizC.u8string() + "\"";
            if (verbose){
                std::cout << "Including miniz.c: " << minizC.u8string() << "\n";
            }
        }
    }

    cmd += " -o \"" + outExe.u8string() + "\""
        + " -L \"" + sdlLib.u8string() + "\""
        + " -lSDL3";

    if (!ffLib.empty()){
        cmd += " -L \"" + ffLib.u8string() + "\""
               " -lavformat -lavcodec -lswscale -lswresample -lavutil";
    }

    #ifdef _WIN32
        cmd += " -lopengl32 -lgdi32 -luser32 -lshell32 -lkernel32";
        cmd += " -lole32 -loleaut32 -limm32 -lwinmm -lversion -luuid -ladvapi32 -lsetupapi";
        cmd += " -lwinhttp";
        if (guiMode){
            cmd += " -mwindows";
        }
    #endif

    if (verbose){
        std::cout << "\nCompiling: " << cmd << std::endl;
    }

    int ret;
    #ifdef _WIN32
        int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, NULL, 0);
        if (wlen <= 0){
            errOut = "Cannot convert command to UTF-16";
            std::error_code ec;
            fs::remove(tmpCpp, ec);
            return false;
        }
        std::wstring wcmd(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, &wcmd[0], wlen);

        STARTUPINFOW si = {0};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {0};

        if (CreateProcessW(NULL, &wcmd[0], NULL, NULL, 0, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, (unsigned long)-1);
            unsigned long exitCode = 0;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            ret = (int)exitCode;
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            ret = -1;
        }
    #else
        std::string fullCmd = "\"" + cmd + "\"";
        ret = std::system(fullCmd.c_str());
    #endif

    std::error_code ec;
    fs::remove(tmpCpp, ec);

    if (ret != 0){
        errOut = "Compilation failed (exit code " + std::to_string(ret) + ")";
        return false;
    }

    if (!fs::exists(outExe)){
        errOut = "g++ 返回成功，但输出文件不存在：" + outExe.u8string() + "\n"
                "  请检查同目录下是否有 a.exe，或 g++ 是否把文件写到了别处。\n"
                "  当前目录：" + fs::current_path().u8string();
        return false;
    }

    if (copyDlls){
        auto copyDllsWithPrefix = [&](const fs::path& binDir,
                                      const std::vector<std::string>& prefixes){
            if (binDir.empty() || !fs::exists(binDir) || !fs::is_directory(binDir)) return;
            for (const auto& entry : fs::directory_iterator(binDir)){
                if (entry.path().extension() != ".dll") continue;

                std::string name = entry.path().filename().u8string();
                std::string lower = name;
                for (auto& c : lower) c = (char)std::tolower((unsigned char)c);

                bool matched = false;
                for (const auto& p : prefixes){
                    if (lower.rfind(p, 0) == 0){ matched = true; break; }
                }
                if (!matched) continue;

                fs::path target = outExe.parent_path() / entry.path().filename();
                std::error_code ec2;

                bool needCopy = true;
                if (fs::exists(target, ec2)){
                    auto s1 = fs::file_size(entry.path(), ec2);
                    auto s2 = fs::file_size(target, ec2);
                    if (!ec2 && s1 == s2) needCopy = false;
                }
                if (needCopy){
                    fs::copy_file(entry.path(), target,
                                  fs::copy_options::overwrite_existing, ec2);
                    if (ec2){
                        std::cerr << "Warning: cannot copy "
                                  << entry.path().filename().u8string()
                                  << ": " << ec2.message() << std::endl;
                    } else if (verbose){
                        std::cout << "Copied "
                                  << entry.path().filename().u8string()
                                  << " to " << target.parent_path().u8string()
                                  << std::endl;
                    }
                }
            }
        };

        copyDllsWithPrefix(sdlBin, {"sdl3"});
        if (!ffBin.empty()){
            copyDllsWithPrefix(ffBin, {
                "avformat", "avcodec", "swscale", "swresample", "avutil"
            });
        }
    }

    if (ret == 0 && fs::exists(outExe)) {
        std::error_code ec2;
        fs::copy_file(outExe, cacheExe, fs::copy_options::overwrite_existing, ec2);
    }

    return true;
}

// ============================================================
// 子命令
// ============================================================
static int cmdHelp(){
    std::cout <<
        "Vox compiler CLI (v" << VOX_VERSION << ")\n"
        "\n"
        "Usage:\n"
        "  vox run <file.vox>              Compile and run a .vox file\n"
        "  vox run <file.vox> -v           Same, but also print source / AST / C code\n"
        "  vox run <file.vox> --window     Copy SDL3.dll (and other DLLs) next to the exe\n"
        "  vox run <file.vox> --gui        Build as Windows GUI app (no console window)\n"
        "  vox run <file.vox> --op         Enable C++ compiler optimizations (-O2) for release\n"
        "\n"
        "  vox install-extension         Install the Vox language extension for VS Code\n"
        "  vox cpp <MinGW-root>          Set MinGW root (e.g. D:\\MinGW)\n"
        "  vox clean                     Clear compilation cache\n"
        "  vox -V | -version             Print version\n"
        "  vox help                      Show this help\n"
        "\n"
        "Config file: " << configPath().u8string() << "\n";
    return 0;
}

static int cmdVersion(){
    std::cout << VOX_VERSION << std::endl;
    return 0;
}

static int cmdSetCpp(const std::string& rootStr){
    if (rootStr.empty()){
        std::cerr << "Usage: vox cpp <MinGW-root>\n";
        return 1;
    }
    fs::path root = fs::u8path(rootStr);
    if (!fs::is_directory(root)){
        std::cerr << "Not a directory: " << root.u8string() << "\n";
        return 1;
    }

#ifdef _WIN32
    fs::path gxx = root / "bin" / "g++.exe";
    if (!fs::exists(gxx)) gxx = root / "g++.exe";
#else
    fs::path gxx = root / "bin" / "g++";
    if (!fs::exists(gxx)) gxx = root / "g++";
#endif
    if (!fs::exists(gxx)) {
        std::cerr << "Warning: g++ not found in " << root.u8string()
                  << "\n  (checked bin/g++ and g++ directly)\n";
    }

    if (!writeMinGWRoot(root)){
        std::cerr << "Cannot write config: " << configPath().u8string() << "\n";
        return 1;
    }
    std::cout << "MinGW root set to: " << root.u8string() << "\n";
    std::cout << "Config file: " << configPath().u8string() << "\n";
    return 0;
}

static int cmdInstallExtension(){
    std::vector<fs::path> searchBases = {
        fs::current_path(),
        exeDir(),
        exeDir().parent_path()
    };

    fs::path vsixPath;
    std::string expectedName = "vox-language-" + std::string(VOX_VERSION) + ".vsix";

    for (const auto& base : searchBases) {
        for (const auto& dirName : {"extensions", "extension"}) {
            fs::path dir = base / dirName;
            if (!fs::exists(dir)) continue;

            fs::path expectedPath = dir / expectedName;
            if (fs::exists(expectedPath)) {
                vsixPath = expectedPath;
                break;
            }

            if (vsixPath.empty()) {
                for (const auto& entry : fs::directory_iterator(dir)) {
                    if (entry.path().extension() == ".vsix") {
                        vsixPath = entry.path();
                    }
                }
            }
        }
        if (!vsixPath.empty() && vsixPath.filename().u8string() == expectedName) {
            break;
        }
    }

    if (vsixPath.empty()) {
        std::cerr << "Error: Cannot find VS Code extension (.vsix file).\n";
        std::cerr << "Looked in:\n";
        for (const auto& base : searchBases) {
            std::cerr << "  " << (base / "extensions").u8string() << "\n";
        }
        return 1;
    }

    if (vsixPath.filename().u8string() != expectedName) {
        std::cout << "Warning: Expected '" << expectedName << "', but found '"
                  << vsixPath.filename().u8string() << "'.\n";
        std::cout << "(Did you accidentally type a lowercase 'L' instead of a '1'?)\n";
    }

    #ifdef _WIN32
    std::string cmdCheck = "where code >nul 2>&1";
    #else
    std::string cmdCheck = "command -v code >/dev/null 2>&1";
    #endif

    if (std::system(cmdCheck.c_str()) != 0) {
        std::cerr << "Error: VS Code CLI ('code') not found in PATH.\n";
        std::cerr << "Please ensure VS Code is installed and added to PATH.\n";
        std::cerr << "  (In VS Code, press Ctrl+Shift+P, type 'Install code command in PATH', and hit Enter)\n";
        return 1;
    }

    std::cout << "Installing VS Code extension: " << vsixPath.filename().u8string() << " ...\n";

    std::string vsixAnsi = utf8_to_ansi(vsixPath.u8string());
    std::string cmdInstall = "code --install-extension \"" + vsixAnsi + "\"";
    int ret = std::system(cmdInstall.c_str());

    if (ret == 0) {
        std::cout << "Extension installed successfully!\n";
        std::cout << "Please restart VS Code to activate the Vox language support.\n";
        return 0;
    } else {
        std::cerr << "Failed to install extension (exit code " << ret << ").\n";
        return 1;
    }
}

static int cmdRun(const std::string& fileStr, bool verbose, bool window, bool gui, bool optimize){
    #ifndef _WIN32
        if (std::system("command -v curl >/dev/null 2>&1") != 0) {
            std::cerr << "Warning: 'curl' not found. urllib will fail on Unix.\n";
        }
    #endif
    fs::path src = fs::u8path(fileStr);
    if (!fs::exists(src)){
        std::cerr << "File not found: " << src.u8string() << "\n";
        return 1;
    }

    fs::path srcAbs = fs::absolute(src);
    fs::path includeDir = srcAbs.parent_path();
    std::string code = readFileText(srcAbs);
    if (code.empty()){
        std::cerr << "Empty or unreadable file: " << src.u8string() << "\n";
        return 1;
    }

    if (verbose){
        std::cout << "Source Code (" << src.u8string() << "):\n" << code << std::endl;
    }

    std::vector<std::string> searchDirs;

    searchDirs.push_back(includeDir.u8string());

    for (const auto& d : g_extraSearchDirs){
        searchDirs.push_back(d);
    }

    {
        const char* voxPath = std::getenv("VOX_PATH");
        if (voxPath && *voxPath){
            std::string sp = ansiToUtf8(voxPath);
            std::string cur;
        #ifdef _WIN32
            const char sep = ';';
        #else
            const char sep = ':';
        #endif
            for (char c : sp){
                if (c == sep){
                    if (!cur.empty()){ searchDirs.push_back(cur); cur.clear(); }
                } else {
                    cur += c;
                }
            }
            if (!cur.empty()){ searchDirs.push_back(cur); }
        }
    }

    searchDirs.push_back(exeDir().u8string());
    searchDirs.push_back((exeDir() / "Lib").u8string());
    searchDirs.push_back(fs::current_path().u8string());

    {
        std::set<std::string> seen;
        std::vector<std::string> uniq;
        for (const auto& d : searchDirs){
            if (!d.empty() && seen.insert(d).second){
                uniq.push_back(d);
            }
        }
        searchDirs = std::move(uniq);
    }

    std::string generated;
    try {
        Lexer lexer(code, src.u8string());
        std::vector<Token> tokens = lexer.tokenize();

        Parser parser(tokens, code, src.u8string(), includeDir.u8string(),
                      nullptr, searchDirs);
        AstNodePtr ast = parser.parse();

        {
            const char* YELLOW = g_color_enabled ? "\033[33m" : "";
            const char* RESET  = g_color_enabled ? "\033[0m"  : "";
            for (const auto& w : parser.warnings()){
                std::cout << YELLOW << w << RESET;
            }
        }

        if (verbose){
            std::cout << "\nAST:\n" << ast->toString() << std::endl;
        }

        generated = ast->toC();

        if (verbose){
            std::cout << "\nGenerated C Code:\n" << generated << std::endl;
        }
    }
    catch (const CompileError& e){
        std::string typeName =
            (e.kind() >= 0 && e.kind() < static_cast<int>(exceptions.size()))
                ? exceptions[e.kind()]
                : "UnknownException";

        bool color = g_color_enabled;
        const char* RESET  = color ? "\033[0m"    : "";
        const char* HEADER = color ? "\033[1;31m" : "";
        const char* FILE_C = color ? "\033[1;33m" : "";
        const char* CARET  = color ? "\033[1;31m" : "";
        const char* HINT   = color ? "\033[2m"    : "";

        std::cout << HEADER << "Traceback:" << RESET << std::endl;
        std::cout << "  " << FILE_C
                  << "File <" << e.path() << "> line " << e.line()
                  << RESET << std::endl;
        std::cout << "    " << e.code() << std::endl;

        if (e.caretCol() >= 0){
            std::string caretLine(e.caretCol(), ' ');
            caretLine += '^';
            for (int i = 1; i < e.caretLen(); ++i) {
                caretLine += '~';
            }

            std::cout << "    " << CARET << caretLine << RESET << std::endl;

            if (!e.caretHint().empty()){
                std::string hintLine(e.caretCol(), ' ');
                std::cout << "    " << HINT
                          << hintLine << "(" << e.caretHint() << ")"
                          << RESET << std::endl;
            }
        }

        std::cout << HEADER << typeName << RESET << ": " << e.message() << std::endl;
        return 1;
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
    catch (...) {
        std::cerr << "Fatal: unknow exception" << std::endl;
        return 1;
    }

    fs::path outExe = srcAbs;
    outExe.replace_extension(".exe");

    std::string err;
    if (!compileToExe(generated, outExe, includeDir, searchDirs, err, verbose, window, gui, optimize)){
        std::cerr << err << std::endl;
        return 1;
    }

    if (verbose){
        std::cout << "Build succeeded: " << outExe.u8string() << "\n";
        std::cout << "Running...\n" << std::endl;
    }

    std::string runCmd = "\"" + outExe.u8string() + "\"";
    int ret = run_process(runCmd);

    if (verbose){
        std::cout << "\nProcess exited with code " << ret << std::endl;
    }

    if (ret != 0) {
        std::cerr << "Program exited with code " << ret << std::endl;
    }
    return ret;
}

// ============================================================
// main
// ============================================================
int main(int argc, char** argv){
    g_color_enabled = init_console_color();

    if (argc < 2){
        return cmdHelp();
    }

    std::string sub = argv[1];

    if (sub == "help" || sub == "-h" || sub == "--help"){
        return cmdHelp();
    }

    if (sub == "-V" || sub == "-version" || sub == "--version"){
        return cmdVersion();
    }

    if (sub == "cpp"){
        std::string root = (argc >= 3) ? ansiToUtf8(argv[2]) : "";
        return cmdSetCpp(root);
    }

    if (sub == "install-extension"){
        return cmdInstallExtension();
    }

    if (sub == "run"){
        if (argc < 3){
            std::cerr << "Usage: vox run <file.vox> [-v] [--window] [-I path]\n";
            return 1;
        }

        std::string file;
        bool verbose = false;
        bool window = false;
        bool gui = false;
        bool optimize = false;
        g_extraSearchDirs.clear();

        for (int i = 2; i < argc; ++i){
            std::string a = ansiToUtf8(argv[i]);
            if (a == "-v" || a == "--verbose"){
                verbose = true;
            } else if (a == "--window" || a == "-w"){
                window = true;
            } else if (a == "--gui" || a == "-g"){
                gui = true;
            } else if (a == "--op" || a == "-O"){
                optimize = true;
            } else if (a == "-I" && i + 1 < argc){
                g_extraSearchDirs.push_back(ansiToUtf8(argv[++i]));
            } else if (a.rfind("-I", 0) == 0 && a.size() > 2){
                g_extraSearchDirs.push_back(a.substr(2));
            } else if (file.empty()){
                file = a;
            } else {
                std::cerr << "Unexpected argument: " << a << "\n";
                return 1;
            }
        }

        if (file.empty()){
            std::cerr << "Usage: vox run <file.vox> [-v] [--window] [-I path]\n";
            return 1;
        }

        return cmdRun(file, verbose, window, gui, optimize);
    }

    if (sub == "clean") {
        fs::path cacheDir = fs::temp_directory_path() / "vox_cache";
        if (fs::exists(cacheDir)) {
            std::error_code ec;
            fs::remove_all(cacheDir, ec);
            if (!ec) {
                std::cout << "Cache cleared successfully!" << std::endl;
                return 0;
            } else {
                std::cerr << "Failed to clear cache: " << ec.message() << std::endl;
                return 1;
            }
        }
        std::cout << "Cache is already empty." << std::endl;
        return 0;
    }

    std::cerr << "Unknown command: " << sub << "\n\n";
    cmdHelp();
    return 1;
}