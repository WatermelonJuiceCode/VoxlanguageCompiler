#ifndef LEXER_H
#define LEXER_H

#include <iostream>
#include <vector>
#include <string>
#include <cctype>
#include <unordered_map>
#include <utility>

#include "token.h"
#include "exception.h"
#include "variables.h"

class Lexer{
public:
    Lexer(std::string text, std::string fileName){
        _text = std::move(text);
        _fileName = std::move(fileName);
        _pos = 0;
        _line = 1;
    }

    std::vector<Token> tokenize(){
        std::vector<Token> tokens;
        while (true){
            Token token = nextToken();
            tokens.push_back(token);
            if (token.type == TokenType::END) break;
        }
        return tokens;
    }

    std::string getLine(int line) const {
        if (line <= 0) return "";
        std::size_t start = 0;
        int currentLine = 1;
        while (currentLine < line){
            std::size_t pos = _text.find('\n', start);
            if (pos == std::string::npos) return "";
            start = pos + 1;
            ++currentLine;
        }
        std::size_t end = _text.find('\n', start);
        if (end == std::string::npos) end = _text.size();
        std::string raw = _text.substr(start, end - start);
        // 展开 Tab
        std::string expanded;
        for (char ch : raw){
            if (ch == '\t') expanded += "    ";
            else            expanded += ch;
        }
        return expanded;
    }

private:
    std::string _text, _fileName;
    int _pos;
    int _line = 1;
    int _lastNewlinePos = -1;

    static TokenType keywordType(const std::string& word){
        static const std::unordered_map<std::string, TokenType> keywords = {
            {"int",       TokenType::KwInt},
            {"string",    TokenType::KwString},
            {"bool",      TokenType::KwBool},
            {"char",      TokenType::KwChar},
            {"void",      TokenType::KwVoid},
            {"int8",      TokenType::KwInt8},
            {"int16",     TokenType::KwInt16},
            {"int32",     TokenType::KwInt32},
            {"int64",     TokenType::KwInt64},
            {"uint8",     TokenType::KwUInt8},
            {"uint16",    TokenType::KwUInt16},
            {"uint32",    TokenType::KwUInt32},
            {"uint64",    TokenType::KwUInt64},
            {"float",     TokenType::KwFloat}, 
            {"double",    TokenType::KwDouble}, 
            {"return",    TokenType::KwReturn},
            {"const",     TokenType::KwConst},
            {"ref",       TokenType::KwRef},
            {"any",       TokenType::KwAny},
            {"raise",     TokenType::KwRaise},
            {"true",      TokenType::True},
            {"false",     TokenType::False},
            {"null",      TokenType::Null},
            {"using",     TokenType::Using},
            {"class",     TokenType::KwClass},
            {"new",       TokenType::KwNew},
            {"self",      TokenType::KwSelf},
            {"super",     TokenType::KwSuper},   // ★
            {"static",    TokenType::KwStatic},
            {"override",  TokenType::KwOverride},
            {"interface", TokenType::KwInterface},
            {"abstract",  TokenType::KwAbstract},   // ★
            {"public",    TokenType::KwPublic},
            {"private",   TokenType::KwPrivate},
            {"protected", TokenType::KwProtected},
            {"for",       TokenType::KwFor},
            {"if",        TokenType::KwIf},
            {"else",      TokenType::KwElse},
            {"while",     TokenType::KwWhile},
            {"do",        TokenType::KwDo},
            {"foreach",   TokenType::KwForeach},
            {"in",        TokenType::KwIn},
            {"is",        TokenType::KwIs},
            {"enum",      TokenType::KwEnum},
            {"struct",    TokenType::KwStruct},
            {"template",  TokenType::KwTemplate},   // ★
            {"typename",  TokenType::KwTypename},   // ★
            {"typeof",    TokenType::KwTypeof},
            {"namespace", TokenType::KwNamespace},
            {"extern",    TokenType::KwExtern},
            {"List",      TokenType::KwList},
            {"Dict",      TokenType::KwDict},
            {"FinalList", TokenType::KwFinalList},
            {"FinalDict", TokenType::KwFinalDict},
            {"try",       TokenType::KwTry},
            {"catch",     TokenType::KwCatch},
            {"finally",   TokenType::KwFinally},
            {"break",     TokenType::KwBreak},
            {"continue",  TokenType::KwContinue},
            {"switch",    TokenType::KwSwitch},
            {"case",      TokenType::KwCase},
            {"default",   TokenType::KwDefault},
            {"with",      TokenType::KwWith},
        };
        auto it = keywords.find(word);
        if (it == keywords.end()) return TokenType::Identifier;
        return it->second;
    }

    static bool decodeEscape(const std::string& text, int& pos, char& out){
        char c = text[pos + 1];
        switch (c){
        case 'n':  out = '\n'; pos += 2; return true;
        case 't':  out = '\t'; pos += 2; return true;
        case 'r':  out = '\r'; pos += 2; return true;
        case 'a':  out = '\a'; pos += 2; return true;
        case 'b':  out = '\b'; pos += 2; return true;
        case 'f':  out = '\f'; pos += 2; return true;
        case 'v':  out = '\v'; pos += 2; return true;
        case '0':  out = '\0'; pos += 2; return true;
        case '\\': out = '\\'; pos += 2; return true;
        case '"':  out = '"';  pos += 2; return true;
        case '\'': out = '\''; pos += 2; return true;
        case 'x': {
            int p = pos + 2;
            unsigned int val = 0;
            int digits = 0;
            while (p < static_cast<int>(text.size()) &&
                std::isxdigit(static_cast<unsigned char>(text[p]))){
                char h = text[p];
                int d;
                if (h >= '0' && h <= '9')      d = h - '0';
                else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
                else                           d = h - 'A' + 10;
                val = val * 16u + static_cast<unsigned>(d);
                ++p; ++digits;
            }
            if (digits == 0){
                out = 'x';
                pos += 2;
                return true;
            }
            out = static_cast<char>(val & 0xFF);
            pos = p;
            return true;
        }
        default:
            out = c;
            pos += 2;
            return true;
        }
    }

    Token nextToken(){
        skipSpace();
        int tokCol = _pos - _lastNewlinePos - 1;      // ★ 0-indexed 列
        if (tokCol < 0) tokCol = 0;
        if (_pos >= _text.size())
            return Token(TokenType::END, "", _line, tokCol);

        char current = _text.at(_pos);

        if (isdigit(static_cast<unsigned char>(current))){
            std::string number;
            bool isFloat = false;

            // ---- 十六进制：0x / 0X ----
            if (current == '0' &&
                _pos + 1 < (int)_text.size() &&
                (_text.at(_pos + 1) == 'x' || _text.at(_pos + 1) == 'X')){
                number += _text.at(_pos); _pos++;   // '0'
                number += _text.at(_pos); _pos++;   // 'x' 或 'X'
                while (_pos < (int)_text.size() &&
                    std::isxdigit(static_cast<unsigned char>(_text.at(_pos)))){
                    number += _text.at(_pos); _pos++;
                }
                return Token(TokenType::Number, number, _line);
            }

            // ---- 二进制：0b / 0B ----
            if (current == '0' &&
                _pos + 1 < (int)_text.size() &&
                (_text.at(_pos + 1) == 'b' || _text.at(_pos + 1) == 'B')){
                number += _text.at(_pos); _pos++;   // '0'
                number += _text.at(_pos); _pos++;   // 'b' 或 'B'
                while (_pos < (int)_text.size() &&
                    (_text.at(_pos) == '0' || _text.at(_pos) == '1')){
                    number += _text.at(_pos); _pos++;
                }
                return Token(TokenType::Number, number, _line);
            }

            // ---- 八进制：0o / 0O ----
            if (current == '0' &&
                _pos + 1 < (int)_text.size() &&
                (_text.at(_pos + 1) == 'o' || _text.at(_pos + 1) == 'O')){
                number += _text.at(_pos); _pos++;   // '0'
                number += _text.at(_pos); _pos++;   // 'o' 或 'O'
                while (_pos < (int)_text.size() &&
                    _text.at(_pos) >= '0' && _text.at(_pos) <= '7'){
                    number += _text.at(_pos); _pos++;
                }
                return Token(TokenType::Number, number, _line);
            }

            // ---- 十进制（原逻辑） ----
            while (_pos < _text.size() && isdigit(static_cast<unsigned char>(_text.at(_pos)))){
                number += _text.at(_pos); _pos++;
            }

            // 小数部分：'.' 后面必须跟数字
            if (_pos + 1 < _text.size() &&
                _text.at(_pos) == '.' &&
                isdigit(static_cast<unsigned char>(_text.at(_pos + 1)))){
                isFloat = true;
                number += _text.at(_pos); _pos++;
                while (_pos < _text.size() && isdigit(static_cast<unsigned char>(_text.at(_pos)))){
                    number += _text.at(_pos); _pos++;
                }
            }

            // 指数部分：e / E
            if (_pos < _text.size() &&
                (_text.at(_pos) == 'e' || _text.at(_pos) == 'E')){
                std::size_t save = _pos;
                std::string exp;
                exp += _text.at(_pos); _pos++;
                if (_pos < _text.size() &&
                    (_text.at(_pos) == '+' || _text.at(_pos) == '-')){
                    exp += _text.at(_pos); _pos++;
                }
                if (_pos < _text.size() && isdigit(static_cast<unsigned char>(_text.at(_pos)))){
                    while (_pos < _text.size() &&
                        isdigit(static_cast<unsigned char>(_text.at(_pos)))){
                        exp += _text.at(_pos); _pos++;
                    }
                    isFloat = true;
                    number += exp;
                } else {
                    _pos = save;
                }
            }

            // 后缀 f / F
            char suffix = 0;
            if (_pos < _text.size() &&
                (_text.at(_pos) == 'f' || _text.at(_pos) == 'F')){
                suffix = _text.at(_pos);
                _pos++;
            }

            if (isFloat || suffix){
                if (suffix) number += "f";
                return Token(TokenType::FloatLiteral, number, _line);
            }
            return Token(TokenType::Number, number, _line);
        }
        if (isalpha(static_cast<unsigned char>(current)) || current == '_'){
            std::string identifier;
            while (_pos < _text.size() &&
                (isalnum(static_cast<unsigned char>(_text.at(_pos))) || _text.at(_pos) == '_')){
                identifier += _text.at(_pos); _pos++;
            }
            return Token(keywordType(identifier), identifier, _line);
        }

        if (current == '"'){
            int strLine = _line;
            _pos++;
            std::string value;
            while (_pos < _text.size() && _text.at(_pos) != '"'){
                char c = _text.at(_pos);
                if (c == '\\' && _pos + 1 < static_cast<int>(_text.size())){
                    char out;
                    decodeEscape(_text, _pos, out);
                    value += out;
                    continue;
                }
                if (c == '\n') _line++;
                value += c;
                _pos++;
            }
            if (_pos < _text.size()) _pos++;
            return Token(TokenType::StringLiteral, value, strLine);
        }

        if (current == '\''){
            int chrLine = _line;
            _pos++;
            std::string value;
            while (_pos < _text.size() && _text.at(_pos) != '\''){
                char c = _text.at(_pos);
                if (c == '\\' && _pos + 1 < static_cast<int>(_text.size())){
                    char out;
                    decodeEscape(_text, _pos, out);
                    value += out;
                    continue;
                }
                if (c == '\n') _line++;
                value += c;
                _pos++;
            }
            if (_pos < _text.size()) _pos++;
            return Token(TokenType::CharLiteral, value, chrLine);
        }

        // 两字符运算符
        if (_pos + 1 < _text.size()){
            char nxt = _text.at(_pos + 1);
            if (current == '+' && nxt == '+'){ _pos += 2; return Token(TokenType::PlusPlus, "++", _line, tokCol); }
            if (current == '+' && nxt == '='){ _pos += 2; return Token(TokenType::PlusEqual, "+=", _line, tokCol); }
            if (current == '-' && nxt == '-'){ _pos += 2; return Token(TokenType::SubSub, "--", _line, tokCol); }
            if (current == '-' && nxt == '='){ _pos += 2; return Token(TokenType::SubEqual, "-=", _line, tokCol); }
            if (current == '*' && nxt == '='){ _pos += 2; return Token(TokenType::MulEqual, "*=", _line, tokCol); }
            if (current == '/' && nxt == '='){ _pos += 2; return Token(TokenType::DivEqual, "/=", _line, tokCol); }
            if (current == '%' && nxt == '='){ _pos += 2; return Token(TokenType::ModEqual, "%=", _line, tokCol); }
            if (current == '<' && nxt == '='){ _pos += 2; return Token(TokenType::LessEqual, "<=", _line, tokCol); }
            if (current == '>' && nxt == '='){ _pos += 2; return Token(TokenType::GreaterEqual, ">=", _line, tokCol); }
            if (current == '=' && nxt == '='){ _pos += 2; return Token(TokenType::EqualEqual, "==", _line, tokCol); }
            if (current == '=' && nxt == '>'){ _pos += 2; return Token(TokenType::Arrow, "=>", _line, tokCol); }
            if (current == '-' && nxt == '>'){ _pos += 2; return Token(TokenType::ArrowRight, "->", _line, tokCol); }
            if (current == '!' && nxt == '='){ _pos += 2; return Token(TokenType::NotEqual, "!=", _line, tokCol); }
            if (current == '<' && nxt == '<'){ _pos += 2; return Token(TokenType::Shl, "<<", _line, tokCol); }
            if (current == '>' && nxt == '>'){ _pos += 2; return Token(TokenType::Shr, ">>", _line, tokCol); }
            if (current == '&' && nxt == '&'){ _pos += 2; return Token(TokenType::AmpAmp, "&&", _line, tokCol); }
            if (current == '|' && nxt == '|'){ _pos += 2; return Token(TokenType::PipePipe, "||", _line, tokCol); }
        }

        _pos++;
        switch (current){
        case '+': return Token(TokenType::Plus, "+", _line, tokCol);
        case '-': return Token(TokenType::Sub, "-", _line, tokCol);
        case '*': return Token(TokenType::Mul, "*", _line, tokCol);
        case '/': return Token(TokenType::Div, "/", _line, tokCol);
        case '%': return Token(TokenType::Mod, "%", _line, tokCol);
        case '<': return Token(TokenType::Less, "<", _line, tokCol);
        case '>': return Token(TokenType::Greater, ">", _line, tokCol);
        case '&': return Token(TokenType::Amp, "&", _line, tokCol);
        case '!': return Token(TokenType::Bang, "!", _line, tokCol);
        case '|': return Token(TokenType::Pipe, "|", _line, tokCol);
        case '^': return Token(TokenType::Caret, "^", _line, tokCol);
        case '~': return Token(TokenType::Tilde, "~", _line, tokCol);
        case '?': return Token(TokenType::Question, "?", _line, tokCol);
        case '@': return Token(TokenType::At, "@", _line, tokCol);
        case '(': return Token(TokenType::LParen, "(", _line, tokCol);
        case ')': return Token(TokenType::RParen, ")", _line, tokCol);
        case '{': return Token(TokenType::LBrace, "{", _line, tokCol);
        case '}': return Token(TokenType::RBrace, "}", _line, tokCol);
        case '[': return Token(TokenType::LBracket, "[", _line, tokCol);
        case ']': return Token(TokenType::RBracket, "]", _line, tokCol);
        case '=': return Token(TokenType::Equal, "=", _line, tokCol);
        case ';': return Token(TokenType::Semicolon, ";", _line, tokCol);
        case ',': return Token(TokenType::Comma, ",", _line, tokCol);
        case '.': return Token(TokenType::Dot, ".", _line, tokCol);
        case ':': return Token(TokenType::Colon, ":", _line, tokCol);

        default:
            std::cout << Exception(exceptions[0],
                                "name '" + std::string(1, current) + "' is not defined",
                                _fileName, getLine(_line), _line).exception()
                    << std::endl;
            return Token(TokenType::END, "", _line);
        }
    }

    // ===================== skipSpace =====================
    void skipSpace(){
        while (_pos < _text.size()){
            char c = _text.at(_pos);

            // 换行
            if (c == '\n'){ _line++; _lastNewlinePos = _pos; _pos++; }

            // 空白
            else if (c == ' ' || c == '\t' || c == '\r'){ _pos++; }

            // 单行注释 // 和 ///
            else if (c == '/' && _pos + 1 < _text.size() && _text.at(_pos + 1) == '/'){
                while (_pos < _text.size() && _text.at(_pos) != '\n') _pos++;
            }

            // C 风格多行注释 /* ... */
            else if (c == '/' && _pos + 1 < _text.size() && _text.at(_pos + 1) == '*'){
                _pos += 2;
                while (_pos + 1 < _text.size() &&
                       !(_text.at(_pos) == '*' && _text.at(_pos + 1) == '/')){
                    if (_text.at(_pos) == '\n'){ _line++; _lastNewlinePos = _pos; }
                    _pos++;
                }
                if (_pos + 1 < _text.size()) _pos += 2;
                else _pos = _text.size();
            }

            // Vox 风格多行注释 /- ... -/
            else if (c == '/' && _pos + 1 < _text.size() && _text.at(_pos + 1) == '-'){
                _pos += 2;
                while (_pos + 1 < _text.size() &&
                       !(_text.at(_pos) == '-' && _text.at(_pos + 1) == '/')){   // ★ '-'
                    if (_text.at(_pos) == '\n'){ _line++; _lastNewlinePos = _pos; }
                    _pos++;
                }
                if (_pos + 1 < _text.size()) _pos += 2;
                else _pos = _text.size();
            }

            else break;
        }
    }
};

#endif