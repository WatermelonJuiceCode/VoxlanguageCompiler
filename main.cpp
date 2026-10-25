#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "lexer.h"
#include "token.h"
#include "Parser.h"
#include "AstNodes.h"
#include "exception.h"
#include "variables.h"

namespace fs = std::filesystem;

static const char* VOX_VERSION = "1.0.0";

// ---------------- 配置文件 ----------------
static fs::path exeDir(){
    return fs::current_path();
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

    fs::path runtimeDir = exeDir() / "Code";

    std::string cmd =
        gxx.string()
        + " -std=c++17 -g"
        + " -I \"" + includeDir.string() + "\""
        + " -I \"" + runtimeDir.string() + "\""
        + " \"" + tmpCpp.string() + "\""
        + " -o \"" + outExe.string() + "\""
    #ifdef _WIN32
        + " -lgdi32 -luser32 -lkernel32"
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

    std::string generated;
    try {
        Lexer lexer(code, src.string());
        std::vector<Token> tokens = lexer.tokenize();

        Parser parser(tokens, code, src.string(), includeDir.string());
        AstNodePtr ast = parser.parse();

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

        std::cout << Exception(typeName, e.message(), e.path(), e.code(), e.line()).exception()
                  << std::endl;
        return 1;
    }

    fs::path outExe = srcAbs;
    outExe.replace_extension(".exe");

    std::string err;
    if (!compileToExe(generated, outExe, includeDir, err, verbose)){
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
            std::cerr << "Usage: vox run <file.vox> [-v]\n";
            return 1;
        }

        std::string file;
        bool verbose = false;

        for (int i = 2; i < argc; ++i){
            std::string a = argv[i];
            if (a == "-v" || a == "--verbose"){
                verbose = true;
            } else if (file.empty()){
                file = a;
            } else {
                std::cerr << "Unexpected argument: " << a << "\n";
                return 1;
            }
        }

        if (file.empty()){
            std::cerr << "Usage: vox run <file.vox> [-v]\n";
            return 1;
        }

        return cmdRun(file, verbose);
    }

    std::cerr << "Unknown command: " << sub << "\n\n";
    cmdHelp();
    return 1;
}