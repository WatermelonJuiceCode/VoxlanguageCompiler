#ifndef EXCEPTION_H
#define EXCEPTION_H

#include <iostream>
#include <string>
#include <utility>

// 异常类型编号（与 variables.cpp 中 exceptions 的顺序对应）
enum ExceptionKind {
    EX_NAME          = 0,
    EX_INDEX         = 1,
    EX_INDEX_OUT     = 2,
    EX_TYPE          = 3,
    EX_SYNTAX        = 4,
    EX_ZERO_DIVISION = 5,
    EX_VALUE         = 6,
    EX_ARGUMENT      = 7,
    EX_FILE_NOT_FOUND= 8,
    EX_PERMISSION    = 9,
    EX_IO            = 10,
    EX_MEMORY        = 11
};

class Exception {
public:
    Exception(std::string exceptionType, std::string argText,
              std::string argPath, std::string argExceptionCode, int argLine)
        : _type(std::move(exceptionType)), _text(std::move(argText)),
          _path(std::move(argPath)), _exceptionCode(std::move(argExceptionCode)),
          _line(argLine) {}

    // 给运行时 raise 生成代码用
    Exception(std::string exceptionType, std::string argText)
        : _type(std::move(exceptionType)), _text(std::move(argText)),
          _path(""), _exceptionCode(""), _line(0) {}

    std::string exception() const {
        // 两参构造时 _path/_code/_line 为空，退化为 type: text
        if (_line == 0 && _path.empty() && _exceptionCode.empty()){
            return _type + ": " + _text;
        }
        return "Traceback: \n  File <" + _path + "> line "
               + std::to_string(_line) + "\n    " + _exceptionCode
               + "\n" + _type + ": " + _text;
    }

private:
    std::string _type, _text, _path, _exceptionCode;
    int _line;
};

// 编译期异常：Parser 抛这个，main 负责打印并退出
class CompileError : public std::exception {
public:
    CompileError(int kind, std::string message, std::string path,
                 std::string code, int line,
                 int caretCol = -1, std::string caretHint = "")
        : _kind(kind),
          _message(std::move(message)),
          _path(std::move(path)),
          _code(std::move(code)),
          _line(line),
          _caretCol(caretCol),
          _caretHint(std::move(caretHint)) {}

    int kind() const { return _kind; }
    const std::string& message() const { return _message; }
    const std::string& path()    const { return _path; }
    const std::string& code()    const { return _code; }
    int line() const { return _line; }
    int caretCol() const { return _caretCol; }
    const std::string& caretHint() const { return _caretHint; }

    const char* what() const noexcept override { return _message.c_str(); }

private:
    int _kind;
    std::string _message, _path, _code;
    int _line;
    int _caretCol;
    std::string _caretHint;
};

#endif