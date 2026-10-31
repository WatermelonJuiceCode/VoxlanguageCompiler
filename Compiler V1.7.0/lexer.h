#ifndef LEXER_H
#define LEXER_H

#include <iostream>
#include <vector>
#include <string>
#include <cctype>
#include <unordered_map>
#include <utility>
#include <unordered_set>

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

    std::vector<Token> _pendingTokens;
    std::size_t _pendingIdx = 0;

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
            {"byte",      TokenType::KwByte},
            {"float",     TokenType::KwFloat}, 
            {"double",    TokenType::KwDouble}, 
            {"return",    TokenType::KwReturn},
            {"const",     TokenType::KwConst},
            {"ref",       TokenType::KwRef},
            {"any",       TokenType::KwAny},
            {"raise",     TokenType::KwRaise},
            {"modify",    TokenType::KwModify},
            {"true",      TokenType::True},
            {"false",     TokenType::False},
            {"null",      TokenType::Null},
            {"using",     TokenType::Using},
            {"class",     TokenType::KwClass},
            {"new",       TokenType::KwNew},
            {"self",      TokenType::KwSelf},
            {"super",     TokenType::KwSuper},
            {"static",    TokenType::KwStatic},
            {"override",  TokenType::KwOverride},
            {"interface", TokenType::KwInterface},
            {"abstract",  TokenType::KwAbstract},
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
            {"template",  TokenType::KwTemplate},
            {"typename",  TokenType::KwTypename},
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
            {"signal",    TokenType::KwSignal},
            {"CanAppend", TokenType::KwCanAppend},
            {"Append",    TokenType::KwAppend},
            {"other",     TokenType::KwOther},
            {"friend",    TokenType::KwFriend},
        };
        auto it = keywords.find(word);
        if (it == keywords.end()) return TokenType::Identifier;
        return it->second;
    }

    static bool isCxxReserved(const std::string& word){
        static const std::unordered_set<std::string> reserved = {
            "alignas", "alignof", "and", "and_eq", "asm",
            "auto", "bitand", "bitor", "compl",
            "concept", "consteval", "constexpr", "constinit", "const_cast",
            "co_await", "co_return", "co_yield",
            "decltype", "delete", "dynamic_cast",
            "explicit", "export", "friend", "goto",
            "inline", "long", "mutable", "noexcept",
            "not", "not_eq", "nullptr", "operator", "or", "or_eq",
            "register", "reinterpret_cast", "requires",
            "short", "signed", "sizeof",
            "static_assert", "static_cast", "thread_local", "throw",
            "typeid", "typedef", "union", "unsigned", "volatile",
            "wchar_t", "xor", "xor_eq",
            "char8_t", "char16_t", "char32_t",
            "NULL", "EOF"
        };
        return reserved.count(word) > 0;
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

    [[noreturn]] void raiseUnterminated(const std::string& kind,
                                        int line, int col) const {
        throw CompileError(EX_SYNTAX,
            "unterminated " + kind + " string (missing closing '\"')",
            _fileName, getLine(line), line, col, 
            1,
            "add closing '\"' here");
    }

    void parseInterpolatedString(int strLine, int strCol, bool isRaw = false){
        if (isRaw) {
            _pos += 3;
        } else {
            _pos += 2;
        }
        
        std::vector<std::pair<std::string, bool>> parts;
        std::string litBuf;
        bool closed = false;

        while (_pos < _text.size()){
            char c = _text[_pos];

            if (c == '\\' && !isRaw && _pos + 1 < _text.size()){
                char out_c;
                decodeEscape(_text, _pos, out_c);
                litBuf += out_c;
            } else if (c == '"'){
                if (isRaw && _pos + 1 < _text.size() && _text[_pos + 1] == '"') {
                    litBuf += '"';
                    _pos += 2;
                    continue;
                }
                _pos++;
                closed = true;
                break;
            } else if (c == '{'){
                if (_pos + 1 < _text.size() && _text[_pos + 1] == '{'){
                    litBuf += '{';
                    _pos += 2;
                } else {
                    parts.push_back({litBuf, false});
                    litBuf.clear();

                    int depth = 1;
                    std::size_t start = _pos + 1;
                    _pos++;
                    while (_pos < _text.size() && depth > 0){
                        char ch = _text[_pos];
                        if (ch == '{') depth++;
                        else if (ch == '}') { depth--; if (depth == 0) break; }
                        else if (ch == '\n') _line++;
                        _pos++;
                    }
                    if (depth != 0){
                        throw CompileError(EX_SYNTAX,
                            "unterminated '{' in interpolated string",
                            _fileName, getLine(strLine), strLine, strCol,
                            1,
                            "add closing '}' here");
                    }
                    std::string exprSrc = _text.substr(start, _pos - start);
                    parts.push_back({exprSrc, true});
                    if (_pos < _text.size()) _pos++;
                }
            } else if (c == '}'){
                if (_pos + 1 < _text.size() && _text[_pos + 1] == '}'){
                    litBuf += '}';
                    _pos += 2;
                } else {
                    litBuf += '}';
                    _pos++;
                }
            } else {
                if (c == '\n') _line++;
                litBuf += c;
                _pos++;
            }
        }

        if (!closed){
            raiseUnterminated("interpolated", strLine, strCol);
        }

        parts.push_back({litBuf, false});

        _pendingTokens.clear();
        _pendingIdx = 0;

        bool first = true;
        for (const auto& [text, isExpr] : parts){
            if (!first){
                _pendingTokens.push_back(Token(TokenType::Plus, "+", strLine, strCol));
            }
            first = false;

            if (isExpr){
                Lexer subLex(text, _fileName);
                auto subToks = subLex.tokenize();
                for (auto& t : subToks){
                    if (t.type == TokenType::END) continue;
                    _pendingTokens.push_back(t);
                }
            } else {
                _pendingTokens.push_back(Token(TokenType::StringLiteral, text, strLine, strCol));
            }
        }

        if (_pendingTokens.empty()){
            _pendingTokens.push_back(Token(TokenType::StringLiteral, "", strLine, strCol));
        }
    }

    Token nextToken(){
        if (_pendingIdx < _pendingTokens.size()){
            return _pendingTokens[_pendingIdx++];
        }
        _pendingTokens.clear();
        _pendingIdx = 0;
        skipSpace();
        int tokCol = _pos - _lastNewlinePos - 1;
        if (tokCol < 0) tokCol = 0;
        if (_pos >= _text.size())
            return Token(TokenType::END, "", _line, tokCol);

        char current = _text.at(_pos);

        if (isdigit(static_cast<unsigned char>(current))){
            std::string number;
            bool isFloat = false;

            if (current == '0' &&
                _pos + 1 < (int)_text.size() &&
                (_text.at(_pos + 1) == 'x' || _text.at(_pos + 1) == 'X')){
                number += _text.at(_pos); _pos++;
                number += _text.at(_pos); _pos++;
                while (_pos < (int)_text.size() &&
                    std::isxdigit(static_cast<unsigned char>(_text.at(_pos)))){
                    number += _text.at(_pos); _pos++;
                }
                return Token(TokenType::Number, number, _line, tokCol);
            }

            if (current == '0' &&
                _pos + 1 < (int)_text.size() &&
                (_text.at(_pos + 1) == 'b' || _text.at(_pos + 1) == 'B')){
                number += _text.at(_pos); _pos++;
                number += _text.at(_pos); _pos++;
                while (_pos < (int)_text.size() &&
                    (_text.at(_pos) == '0' || _text.at(_pos) == '1')){
                    number += _text.at(_pos); _pos++;
                }
                return Token(TokenType::Number, number, _line, tokCol);
            }

            if (current == '0' &&
                _pos + 1 < (int)_text.size() &&
                (_text.at(_pos + 1) == 'o' || _text.at(_pos + 1) == 'O')){
                number += _text.at(_pos); _pos++;
                number += _text.at(_pos); _pos++;
                while (_pos < (int)_text.size() &&
                    _text.at(_pos) >= '0' && _text.at(_pos) <= '7'){
                    number += _text.at(_pos); _pos++;
                }
                return Token(TokenType::Number, number, _line, tokCol);
            }

            while (_pos < _text.size() && isdigit(static_cast<unsigned char>(_text.at(_pos)))){
                number += _text.at(_pos); _pos++;
            }

            if (_pos + 1 < _text.size() &&
                _text.at(_pos) == '.' &&
                isdigit(static_cast<unsigned char>(_text.at(_pos + 1)))){
                isFloat = true;
                number += _text.at(_pos); _pos++;
                while (_pos < _text.size() && isdigit(static_cast<unsigned char>(_text.at(_pos)))){
                    number += _text.at(_pos); _pos++;
                }
            }

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

            char suffix = 0;
            if (_pos < _text.size() &&
                (_text.at(_pos) == 'f' || _text.at(_pos) == 'F')){
                suffix = _text.at(_pos);
                _pos++;
            }

            if (isFloat || suffix){
                if (suffix) number += "f";
                return Token(TokenType::FloatLiteral, number, _line, tokCol);
            }
            return Token(TokenType::Number, number, _line, tokCol);
        }
        if (isalpha(static_cast<unsigned char>(current)) || current == '_'){
            std::string identifier;
            while (_pos < _text.size() &&
                (isalnum(static_cast<unsigned char>(_text.at(_pos))) || _text.at(_pos) == '_')){
                identifier += _text.at(_pos); _pos++;
            }
            TokenType type = keywordType(identifier);

            if (type == TokenType::Identifier && isCxxReserved(identifier)){
                return Token(TokenType::Identifier, identifier + "_vox", _line, tokCol);
            }
            return Token(type, identifier, _line, tokCol);
        }

        if ((current == '$' || current == '@') && _pos + 1 < (int)_text.size()){
            char nextChar = _text.at(_pos + 1);
            
            if ((current == '$' && nextChar == '@') || (current == '@' && nextChar == '$')) {
                if (_pos + 2 < (int)_text.size() && _text.at(_pos + 2) == '"') {
                    int strLine = _line;
                    parseInterpolatedString(strLine, tokCol, true);
                    if (_pendingIdx < _pendingTokens.size()){
                        return _pendingTokens[_pendingIdx++];
                    }
                    return Token(TokenType::StringLiteral, "", strLine, tokCol);
                }
            }
            
            if (current == '@' && nextChar == '"') {
                _pos += 2;
                std::string value;
                bool closed = false;
                while (_pos < _text.size()){
                    char ch = _text[_pos];
                    if (ch == '"'){
                        if (_pos + 1 < _text.size() && _text[_pos + 1] == '"'){
                            value += '"';
                            _pos += 2;
                        } else {
                            _pos++;
                            closed = true;
                            break;
                        }
                    } else {
                        if (ch == '\n') _line++;
                        value += ch;
                        _pos++;
                    }
                }
                if (!closed){
                    raiseUnterminated("raw", _line, tokCol);
                }
                return Token(TokenType::StringLiteral, value, _line, tokCol);
            }

            if (current == '$' && nextChar == '"') {
                int strLine = _line;
                parseInterpolatedString(strLine, tokCol, false);
                if (_pendingIdx < _pendingTokens.size()){
                    return _pendingTokens[_pendingIdx++];
                }
                return Token(TokenType::StringLiteral, "", strLine, tokCol);
            }
        }

        if (current == '"'){
            int strLine = _line;
            int strCol = tokCol;
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
            if (_pos >= _text.size()){
                raiseUnterminated("string", strLine, strCol);
            }
            _pos++;
            return Token(TokenType::StringLiteral, value, strLine, tokCol);
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
            return Token(TokenType::CharLiteral, value, chrLine, tokCol);
        }

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
            if (current == '?' && nxt == '?'){ _pos += 2; return Token(TokenType::NullCoalescing, "??", _line, tokCol); }
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
            throw CompileError(EX_NAME, 
                "name '" + std::string(1, current) + "' is not defined",
                _fileName, getLine(_line), _line, tokCol, 1);
        }
    }

    void skipSpace(){
        while (_pos < _text.size()){
            char c = _text.at(_pos);

            if (c == '\n'){ _line++; _lastNewlinePos = _pos; _pos++; }

            else if (c == ' ' || c == '\t' || c == '\r'){ _pos++; }

            else if (c == '/' && _pos + 1 < _text.size() && _text.at(_pos + 1) == '/'){
                while (_pos < _text.size() && _text.at(_pos) != '\n') _pos++;
            }

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

            else if (c == '/' && _pos + 1 < _text.size() && _text.at(_pos + 1) == '-'){
                _pos += 2;
                while (_pos + 1 < _text.size() &&
                       !(_text.at(_pos) == '-' && _text.at(_pos + 1) == '/')){
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