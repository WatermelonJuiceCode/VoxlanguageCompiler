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

static const char* VOX_VERSION = "1.0.0";
static std::vector<std::string> g_extraSearchDirs;

// ---------------- 配置文件 ----------------
#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(
    void* hModule, char* lpFilename, unsigned long nSize);
#endif

static fs::path exeDir(){
#ifdef _WIN32
    char buf[MAX_PATH];
    unsigned long n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
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

static fs::path defaultMinGW(){
#ifdef _WIN32
    return "D:\\MinGW";
#else
    return "/usr";
#endif
}

static fs::path readMinGWRoot(){
    std::ifstream in(configPath());
    if (!in) return defaultMinGW();

    std::string line;
    while (std::getline(in, line)){
        if (line.rfind("mingw=", 0) == 0){
            std::string p = line.substr(6);
            while (!p.empty() && (p.front() == ' ' || p.front() == '"')) p.erase(p.begin());
            while (!p.empty() && (p.back()  == ' ' || p.back()  == '"')) p.pop_back();
            if (!p.empty()) return fs::path(p);
        }
    }
    return defaultMinGW();
}

static bool writeMinGWRoot(const fs::path& root){
    std::ofstream out(configPath(), std::ios::trunc);
    if (!out) return false;
    out << "mingw=" << root.string() << "\n";
    return true;
}

static fs::path gxxPath(){
    fs::path root = readMinGWRoot();
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
    return root / "bin" / "g++.exe";
}

// ---------------- 工具函数 ----------------
static std::string readFileText(const fs::path& p){
    std::ifstream in(p, std::ios::in | std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// 生成到临时 .cpp，编译到 <outExe>，成功返回 true
// includeDir 会作为 -I 传给 g++，让 #include "相对文件" 能被找到
static bool compileToExe(const std::string& generated,
                         const fs::path& outExe,
                         const fs::path& includeDir,
                         const std::vector<std::string>& searchDirs,
                         std::string& errOut,
                         bool verbose){
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
    if (!fs::exists(gxx)){
        errOut = "g++ not found at: " + gxx.string() +
                 "\n  Hint: use 'vox cpp <MinGW-root>' to set it";
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

    fs::path glfwDir = runtimeDir / "vendor" / "glfw";

    std::string cmd =
        gxx.string()
        + " -std=c++17 -g"
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
        " -I \"" + (glfwDir / "include").string() + "\""
        + " \"" + tmpCpp.string() + "\""
        + " -o \"" + outExe.string() + "\""
        + " -L \"" + (glfwDir / "lib").string() + "\""
        + " -lglfw3"
    #ifdef _WIN32
        + " -lopengl32 -lgdi32 -luser32 -lshell32 -lkernel32"
    #endif
        ;

    if (verbose){
        std::cout << "\nCompiling: " << cmd << std::endl;
    }
    int ret = std::system(cmd.c_str());

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

    return true;
}

// ---------------- 子命令 ----------------
static int cmdHelp(){
    std::cout <<
        "Vox compiler CLI (v" << VOX_VERSION << ")\n"
        "\n"
        "Usage:\n"
        "  vox run <file.vox>            Compile and run a .vox file\n"
        "  vox run <file.vox> -v         Same, but also print source / AST / C code\n"
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
    if (!writeMinGWRoot(root)){
        std::cerr << "Cannot write config: " << configPath().string() << "\n";
        return 1;
    }
    std::cout << "MinGW root set to: " << root.string() << "\n";
    std::cout << "Config file: " << configPath().string() << "\n";
    return 0;
}

static int cmdRun(const std::string& fileStr, bool verbose){
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

    // 4) 编译器所在目录（标准库 Code 可能在这）
    searchDirs.push_back(exeDir().string());

    // 5) 编译器父目录 / 上上级 + 它们的 Lib 子目录
    //    覆盖：vox.exe 在 VoxCompile/，标准库在 ../Lib/Code/ 的布局
    {
        fs::path p = exeDir();
        fs::path up1 = p.parent_path();
        if (!up1.empty()){
            searchDirs.push_back(up1.string());            // ../Code
            searchDirs.push_back((up1 / "Lib").string());  // ../Lib/Code ★
        }
        fs::path up2 = up1.parent_path();
        if (!up2.empty()){
            searchDirs.push_back(up2.string());            // ../../Code
            searchDirs.push_back((up2 / "Lib").string());  // ../../Lib/Code ★
        }
    }

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

    fs::path outExe = srcAbs;
    outExe.replace_extension(".exe");

    std::string err;
    if (!compileToExe(generated, outExe, includeDir, searchDirs, err, verbose)){
        std::cerr << err << std::endl;
        return 1;
    }

    if (verbose){
        std::cout << "Build succeeded: " << outExe.string() << "\n";
        std::cout << "Running...\n" << std::endl;
    }

    std::string runCmd = "\"" + outExe.string() + "\"";
    int ret = std::system(runCmd.c_str());

    if (verbose){
        std::cout << "\nProcess exited with code " << ret << std::endl;
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
            std::cerr << "Usage: vox run <file.vox> [-v] [-I path]\n";
            return 1;
        }

        std::string file;
        bool verbose = false;
        g_extraSearchDirs.clear();

        for (int i = 2; i < argc; ++i){
            std::string a = argv[i];
            if (a == "-v" || a == "--verbose"){
                verbose = true;
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
            std::cerr << "Usage: vox run <file.vox> [-v] [-I path]\n";
            return 1;
        }

        return cmdRun(file, verbose);
    }

    std::cerr << "Unknown command: " << sub << "\n\n";
    cmdHelp();
    return 1;
}