#include <cstdio>
#include <cstdlib>
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
// 终端 ANSI 颜色检测（独立于 vox_runtime.h）
// ============================================================
#ifdef _WIN32
extern "C" __declspec(dllimport) void* __stdcall GetStdHandle(unsigned long);
extern "C" __declspec(dllimport) int   __stdcall SetConsoleMode(void*, unsigned long);
extern "C" __declspec(dllimport) int   __stdcall GetConsoleMode(void*, unsigned long*);
extern "C" __declspec(dllimport) int   __stdcall SetConsoleOutputCP(unsigned int);

// ★ 补全字符集转换 API，解决中文路径乱码
extern "C" __declspec(dllimport) int __stdcall MultiByteToWideChar(
    unsigned int CodePage, unsigned long dwFlags, const char* lpMultiByteStr,
    int cbMultiByte, wchar_t* lpWideCharStr, int cchWideChar);

extern "C" __declspec(dllimport) int __stdcall WideCharToMultiByte(
    unsigned int CodePage, unsigned long dwFlags, const wchar_t* lpWideCharStr,
    int cchWideChar, char* lpMultiByteStr, int cbMultiByte,
    const char* lpDefaultChar, int* lpUsedDefaultChar);

#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
#ifndef CP_ACP
#define CP_ACP 0
#endif
#endif

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
    return true;   // 大多数 Unix 终端默认支持 ANSI
#endif
}

namespace fs = std::filesystem;

static const char* VOX_VERSION = "1.5.0";
static std::vector<std::string> g_extraSearchDirs;

// ---------------- 配置文件 ----------------
#ifdef _WIN32
extern "C" unsigned long __stdcall vox_GetModuleFileNameA(
    void* hModule, char* lpFilename, unsigned long nSize)
    __asm__("GetModuleFileNameA");
#endif

static fs::path exeDir(){
#ifdef _WIN32
    char buf[MAX_PATH];
    unsigned long n = vox_GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH){
        return fs::path(buf).parent_path();
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

// 读取配置文件中设置的 MinGW 路径，没有则返回空
static fs::path readMinGWRoot(){
    std::ifstream in(configPath());
    if (!in) return {};

    std::string line;
    while (std::getline(in, line)){
        if (line.rfind("mingw=", 0) == 0){
            std::string p = line.substr(6);
            while (!p.empty() && (p.front() == ' ' || p.front() == '"')) p.erase(p.begin());
            while (!p.empty() && (p.back()  == ' ' || p.back()  == '"')) p.pop_back();
            if (!p.empty()) return fs::path(p);
        }
    }
    return {};
}

static bool writeMinGWRoot(const fs::path& root){
    std::ofstream out(configPath(), std::ios::trunc);
    if (!out) return false;
    out << "mingw=" << root.string() << "\n";
    return true;
}

static fs::path gxxPath(){
    // 1. 优先使用 config 文件里的路径
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

    // 2. 其次查找同目录下的 mingw64
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

    // 3. 都没找到，返回空路径
    return {};
}

#ifdef _WIN32
// ★ 将 UTF-8 字符串转换为 Windows 本地 ANSI 编码（GBK），解决中文路径乱码
static std::string utf8_to_ansi(const std::string& utf8) {
    if (utf8.empty()) return "";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
    if (wlen == 0) return "";
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], wlen);

    int alen = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (alen == 0) return "";
    std::string astr(alen, 0);
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &astr[0], alen, NULL, NULL);
    if (!astr.empty() && astr.back() == '\0') astr.pop_back();
    return astr;
}

// ★ 进程创建相关的 API
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
#endif

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

// ---------------- 工具函数 ----------------
static std::string readFileText(const fs::path& p){
    std::ifstream in(p, std::ios::in | std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// ★ 跨平台执行命令并等待结束
static int run_process(const std::string& cmd) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, NULL, 0);
    std::wstring wcmd(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, &wcmd[0], wlen);

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    if (CreateProcessW(NULL, &wcmd[0], NULL, NULL, 0, 0, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, (unsigned long)-1); // INFINITE
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

// 生成到临时 .cpp，编译到 <outExe>，成功返回 true
// includeDir 会作为 -I 传给 g++，让 #include "相对文件" 能被找到
static bool compileToExe(const std::string& generated,
                         const fs::path& outExe,
                         const fs::path& includeDir,
                         const std::vector<std::string>& searchDirs,
                         std::string& errOut,
                         bool verbose,
                         bool copyDlls = false,
                         bool guiMode = false){
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    fs::path tmpDir = fs::temp_directory_path();
    fs::path tmpCpp = tmpDir / ("vox_" + std::to_string(std::rand()) + ".cpp");

    {
        std::ofstream out(tmpCpp);
        if (!out){
            errOut = "Cannot write " + tmpCpp.string();
            return false;
        }
        out << generated;
    }

    fs::path gxx = gxxPath();
    if (gxx.empty() || !fs::exists(gxx)){
        errOut =
            "Error: g++ compiler not found.\n\n"
            "Vox looks for the compiler in this order:\n"
            "  1. The path configured in: " + configPath().string() + "\n"
            "  2. Local folder: " + (exeDir() / "mingw64").string() + "\\bin\\g++.exe\n\n"
            "How to fix:\n"
            "  - Put the MinGW folder next to vox.exe and rename it to 'mingw64'\n"
            "    (so that " + (exeDir() / "mingw64" / "bin" / "g++.exe").string() + " exists)\n"
            "  - OR run: vox cpp <path-to-mingw-root>\n"
            "    (e.g., vox cpp \"D:\\MinGW\")";
        std::error_code ec;
        fs::remove(tmpCpp, ec);
        return false;
    }

    // ★ 找 Code/ 目录（遍历所有搜索路径 + 兜底）
    auto findCodeDir = [](const fs::path& base) -> fs::path {
        fs::path candidate = base / "Code";
        if (fs::exists(candidate / "vox_runtime.h")){
            return candidate;
        }
        return fs::path();
    };

    fs::path runtimeDir;
    std::vector<std::string> tried;

    // 1) 遍历所有 searchDirs（含 includeDir / -I / VOX_PATH / exe / cwd）
    for (const auto& d : searchDirs){
        fs::path r = findCodeDir(d);
        if (!r.empty()){
            runtimeDir = r;
            break;
        }
        tried.push_back((fs::path(d) / "Code").string());
    }

    // 2) 兜底：exeDir() / cwd（searchDirs 里可能没加）
    if (runtimeDir.empty()){
        fs::path r = findCodeDir(exeDir());
        if (!r.empty()) runtimeDir = r;
        else tried.push_back((exeDir() / "Code").string());
    }
    if (runtimeDir.empty()){
        fs::path r = findCodeDir(fs::current_path());
        if (!r.empty()) runtimeDir = r;
        else tried.push_back((fs::current_path() / "Code").string());
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

    // ffmpeg
    fs::path ffmpegRoot = vendorDir / "ffmpeg";
    fs::path ffInc, ffLib, ffBin;

    // 探测头文件：必须存在 libavformat/avformat.h
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
    // 探测 lib
    for (const auto& cand : {
        ffmpegRoot / "lib",
        runtimeDir / "vendor" / "ffmpeg" / "lib"
    }){
        if (trySet(ffLib, cand)) break;
    }
    // 探测 bin
    for (const auto& cand : {
        ffmpegRoot / "bin",
        runtimeDir / "vendor" / "ffmpeg" / "bin"
    }){
        if (trySet(ffBin, cand)) break;
    }

    if (verbose){
        std::cout << "FFmpeg include: " << ffInc.string() << "\n";
        std::cout << "FFmpeg lib    : " << ffLib.string() << "\n";
        std::cout << "FFmpeg bin    : " << ffBin.string() << "\n";
    }
    //

    // ---- 头文件目录：按优先级依次探测 ----
    //   1) x86_64-w64-mingw32/include   ← 你这里的实际位置
    //   2) i686-w64-mingw32/include
    //   3) install/include
    //   4) include
    for (const auto& cand : {
        sdl3Root / "x86_64-w64-mingw32" / "include",
        sdl3Root / "i686-w64-mingw32"   / "include",
        sdl3Root / "install"            / "include",
        sdl3Root / "include"
    }){
        // ★ 必须包含 SDL3/SDL.h 才算有效
        std::error_code ec;
        if (fs::exists(cand / "SDL3" / "SDL.h", ec)){
            sdlInc = cand;
            break;
        }
    }

    // ---- 库目录 ----
    for (const auto& cand : {
        sdl3Root / "install"            / "lib",
        sdl3Root / "x86_64-w64-mingw32" / "lib",
        sdl3Root / "i686-w64-mingw32"   / "lib",
        sdl3Root / "lib"
    }){
        if (trySet(sdlLib, cand)) break;
    }

    // ---- DLL 目录 ----
    for (const auto& cand : {
        sdl3Root / "install"            / "bin",
        sdl3Root / "x86_64-w64-mingw32" / "bin",
        sdl3Root / "i686-w64-mingw32"   / "bin",
        sdl3Root / "bin"
    }){
        if (trySet(sdlBin, cand)) break;
    }

    if (sdlInc.empty()){
        errOut = "Cannot find SDL3 headers under: " + sdl3Root.string()
               + "\n  (expected SDL3/SDL.h in one of x86_64-w64-mingw32/include, "
                 "i686-w64-mingw32/include, install/include, include)";
        std::error_code ec;
        fs::remove(tmpCpp, ec);
        return false;
    }
    if (verbose){
        std::cout << "SDL3 include: " << sdlInc.string() << "\n";
        std::cout << "SDL3 lib    : " << sdlLib.string() << "\n";
        std::cout << "SDL3 bin    : " << sdlBin.string() << "\n";
    }

    std::string cmd =
        "\"" + gxx.string() + "\""
        + " -std=c++17 -O1"
        + " -I \"" + includeDir.string() + "\""
        + " -I \"" + runtimeDir.string() + "\"";

    // ★ 用户 -I / VOX_PATH 也传给 g++（去重——跳过已加的）
    {
        std::set<std::string> already;
        already.insert(includeDir.string());
        already.insert(runtimeDir.string());
        for (const auto& d : searchDirs){
            if (!d.empty() && already.insert(d).second){
                cmd += " -I \"" + d + "\"";
            }
        }
    }


    cmd +=
        " -I \"" + sdlInc.string() + "\""
        + " -I \"" + vendorDir.string() + "\"";

    // ★ FFmpeg 头文件路径（如果探测到了）
    if (!ffInc.empty()){
        cmd += " -I \"" + ffInc.string() + "\"";
    }

    cmd += " \"" + tmpCpp.string() + "\""
        + " -o \"" + outExe.string() + "\""
        + " -L \"" + sdlLib.string() + "\""
        + " -lSDL3";

    // ★ FFmpeg 库和链接参数（如果探测到了）
    if (!ffLib.empty()){
        cmd += " -L \"" + ffLib.string() + "\""
               " -lavformat -lavcodec -lswscale -lswresample -lavutil";
    }

    #ifdef _WIN32
        cmd += " -lopengl32 -lgdi32 -luser32 -lshell32 -lkernel32";
        cmd += " -lole32 -loleaut32 -limm32 -lwinmm -lversion -luuid -ladvapi32 -lsetupapi";
        cmd += " -lwinhttp";
    #endif

    #ifdef _WIN32
        // ★ GUI 模式：链接 Windows 子系统，不弹控制台窗口
        if (guiMode){
            cmd += " -mwindows";
        }
    #endif

    if (verbose){
        std::cout << "\nCompiling: " << cmd << std::endl;   
    }

    int ret;
    #ifdef _WIN32
        // ★ 将整个命令串转换为 UTF-16（宽字符）
        int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, NULL, 0);
        std::wstring wcmd(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, &wcmd[0], wlen);

        STARTUPINFOW si = {0};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {0};

        // ★ 直接调用 CreateProcessW，绕开 cmd.exe 的引号剥离逻辑
        if (CreateProcessW(NULL, &wcmd[0], NULL, NULL, 0, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, (unsigned long)-1); // INFINITE
            unsigned long exitCode = 0;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            ret = (int)exitCode;
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            ret = -1;
        }
    #else
        // Linux/macOS 保持原样
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
        errOut = "g++ 返回成功，但输出文件不存在：" + outExe.string() + "\n"
                "  请检查同目录下是否有 a.exe，或 g++ 是否把文件写到了别处。\n"
                "  当前目录：" + fs::current_path().string();
        return false;
    }

    // ★ 只有 --window 模式才自动拷贝 DLL
    if (copyDlls){
        auto copyDllsWithPrefix = [&](const fs::path& binDir,
                                      const std::vector<std::string>& prefixes){
            if (binDir.empty() || !fs::exists(binDir) || !fs::is_directory(binDir)) return;
            for (const auto& entry : fs::directory_iterator(binDir)){
                if (entry.path().extension() != ".dll") continue;

                // ★ 前缀匹配（大小写不敏感）
                std::string name = entry.path().filename().string();
                std::string lower = name;
                for (auto& c : lower) c = (char)std::tolower((unsigned char)c);

                bool matched = false;
                for (const auto& p : prefixes){
                    if (lower.rfind(p, 0) == 0){ matched = true; break; }
                }
                if (!matched) continue;

                fs::path target = outExe.parent_path() / entry.path().filename();
                std::error_code ec;

                bool needCopy = true;
                if (fs::exists(target, ec)){
                    auto s1 = fs::file_size(entry.path(), ec);
                    auto s2 = fs::file_size(target, ec);
                    if (!ec && s1 == s2) needCopy = false;
                }
                if (needCopy){
                    fs::copy_file(entry.path(), target,
                                  fs::copy_options::overwrite_existing, ec);
                    if (ec){
                        std::cerr << "Warning: cannot copy "
                                  << entry.path().filename().string()
                                  << ": " << ec.message() << std::endl;
                    } else if (verbose){
                        std::cout << "Copied "
                                  << entry.path().filename().string()
                                  << " to " << target.parent_path().string()
                                  << std::endl;
                    }
                }
            }
        };

        // ★ SDL3：只复制 SDL3.dll（不复制 SDL3_image.dll 等扩展库）
        copyDllsWithPrefix(sdlBin, {"sdl3"});

        // ★ FFmpeg：只复制核心库（因为链接了 -lavformat -lavcodec 等）
        if (!ffBin.empty()){
            copyDllsWithPrefix(ffBin, {
                "avformat", "avcodec", "swscale", "swresample", "avutil"
            });
        }
    }

    return true;
}

// ---------------- 子命令 ----------------
static int cmdHelp(){
    std::cout <<
        "Vox compiler CLI (v" << VOX_VERSION << ")\n"
        "\n"
        "Usage:\n"
        "  vox run <file.vox>              Compile and run a .vox file\n"
        "  vox run <file.vox> -v           Same, but also print source / AST / C code\n"
        "  vox run <file.vox> --window     Copy SDL3.dll (and other DLLs) next to the exe\n"
        "  vox run <file.vox> --gui        Build as Windows GUI app (no console window)\n"
        "                                  (use this for pure-GUI programs without Debug.log)\n"
        "  vox cpp <MinGW-root>          Set MinGW root (e.g. D:\\MinGW)\n"
        "  vox -V | -version             Print version\n"
        "  vox help                      Show this help\n"
        "\n"
        "Config file: " << configPath().string() << "\n";
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
    fs::path root = rootStr;
    if (!fs::is_directory(root)){
        std::cerr << "Not a directory: " << root.string() << "\n";
        return 1;
    }

    // ★ 检查一下 g++ 是否真的在这
#ifdef _WIN32
    fs::path gxx = root / "bin" / "g++.exe";
    if (!fs::exists(gxx)) gxx = root / "g++.exe";
#else
    fs::path gxx = root / "bin" / "g++";
    if (!fs::exists(gxx)) gxx = root / "g++";
#endif
    if (!fs::exists(gxx)) {
        std::cerr << "Warning: g++ not found in " << root.string()
                  << "\n  (checked bin/g++ and g++ directly)\n";
    }

    if (!writeMinGWRoot(root)){
        std::cerr << "Cannot write config: " << configPath().string() << "\n";
        return 1;
    }
    std::cout << "MinGW root set to: " << root.string() << "\n";
    std::cout << "Config file: " << configPath().string() << "\n";
    return 0;
}

static int cmdRun(const std::string& fileStr, bool verbose, bool window, bool gui){
    #ifndef _WIN32
        if (std::system("command -v curl >/dev/null 2>&1") != 0) {
            std::cerr << "Warning: 'curl' not found. urllib will fail on Unix.\n";
        }
    #endif
    fs::path src = fileStr;
    if (!fs::exists(src)){
        std::cerr << "File not found: " << src.string() << "\n";
        return 1;
    }

    fs::path srcAbs = fs::absolute(src);
    fs::path includeDir = srcAbs.parent_path();

    std::string code = readFileText(srcAbs);
    if (code.empty()){
        std::cerr << "Empty or unreadable file: " << src.string() << "\n";
        return 1;
    }

    if (verbose){
        std::cout << "Source Code (" << src.string() << "):\n" << code << std::endl;
    }

    // ★ 建立模块搜索路径
    std::vector<std::string> searchDirs;

    // 1) 源文件所在目录（最高优先——本地覆盖）
    searchDirs.push_back(includeDir.string());

    // 2) 用户 -I（显式指定，优先于环境变量）
    for (const auto& d : g_extraSearchDirs){
        searchDirs.push_back(d);
    }

    // 3) ★ VOX_PATH 环境变量（分号 / 冒号分隔）
    {
        const char* voxPath = std::getenv("VOX_PATH");
        if (voxPath && *voxPath){
            std::string sp = voxPath;
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

        // 4) 编译器所在目录（标准库 Code 就在这）
    searchDirs.push_back(exeDir().string());

    // 5) ★ 编译器同目录的 Lib 子目录
    //    覆盖：vox.exe + Lib/Code/ 同目录的新布局
    searchDirs.push_back((exeDir() / "Lib").string());

    // 6) 当前工作目录（兜底）
    searchDirs.push_back(fs::current_path().string());

    // ★ 去掉重复目录（保持原顺序）
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
        Lexer lexer(code, src.string());
        std::vector<Token> tokens = lexer.tokenize();

        Parser parser(tokens, code, src.string(), includeDir.string(),
                      nullptr, searchDirs);                       // ★ 传 searchDirs
        AstNodePtr ast = parser.parse();

        // ★ 打印编译警告（黄色）
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

        bool color = g_color_enabled;   // ★ 改成 g_color_enabled
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
            std::string caretLine(e.caretCol() + 1, ' ');
            caretLine[e.caretCol()] = '^';
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
    if (!compileToExe(generated, outExe, includeDir, searchDirs, err, verbose, window, gui)){
        std::cerr << err << std::endl;
        return 1;
    }

    if (verbose){
        std::cout << "Build succeeded: " << outExe.string() << "\n";
        std::cout << "Running...\n" << std::endl;
    }

    std::string runCmd = "\"" + outExe.string() + "\"";
    int ret = run_process(runCmd);

    if (verbose){
        std::cout << "\nProcess exited with code " << ret << std::endl;
    }

    if (ret != 0) {
        std::cerr << "Program exited with code " << ret << std::endl;
    }
    if (verbose) {
        std::cerr << "\nProcess exited with code " << ret << std::endl;
    }
    return ret;
}

int main(int argc, char** argv){
    g_color_enabled = init_console_color();   // ★ 初始化颜色

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
        std::string root = (argc >= 3) ? argv[2] : "";
        return cmdSetCpp(root);
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
        g_extraSearchDirs.clear();

        for (int i = 2; i < argc; ++i){
            std::string a = argv[i];
            if (a == "-v" || a == "--verbose"){
                verbose = true;
            } else if (a == "--window" || a == "-w"){
                window = true;
            } else if (a == "--gui" || a == "-g"){
                gui = true;
            } else if (a == "-I" && i + 1 < argc){
                g_extraSearchDirs.push_back(argv[++i]);
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

        return cmdRun(file, verbose, window, gui);   // ★
    }

    std::cerr << "Unknown command: " << sub << "\n\n";
    cmdHelp();
    return 1;
}