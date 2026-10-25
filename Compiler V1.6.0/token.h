#ifndef TOKEN_H
#define TOKEN_H

#include <iostream>
#include <string>
#include <utility>

#include "tokenType.h"

inline std::string tokenTypeName(TokenType t){
    switch (t){
        case TokenType::Number:        return "Number";
        case TokenType::FloatLiteral:  return "FloatLiteral";
        
        // ---- 运算符 ----
        case TokenType::Plus:             return "+";
        case TokenType::PlusPlus:         return "++";
        case TokenType::PlusEqual:        return "+=";
        case TokenType::Sub:              return "-";
        case TokenType::SubSub:           return "--";
        case TokenType::SubEqual:         return "-=";
        case TokenType::Mul:              return "*";
        case TokenType::MulEqual:         return "*=";
        case TokenType::Div:              return "/";
        case TokenType::DivEqual:         return "/=";
        case TokenType::Mod:              return "%";
        case TokenType::ModEqual:         return "%=";
        case TokenType::Less:             return "<";
        case TokenType::Greater:          return ">";
        case TokenType::LessEqual:        return "<=";
        case TokenType::GreaterEqual:     return ">=";
        case TokenType::EqualEqual:       return "==";
        case TokenType::NotEqual:         return "!=";
        case TokenType::Amp:              return "&";
        case TokenType::AmpAmp:           return "&&";
        case TokenType::Pipe:             return "|";
        case TokenType::PipePipe:         return "||";
        case TokenType::Caret:            return "^";
        case TokenType::Tilde:            return "~";
        case TokenType::Shl:              return "<<";
        case TokenType::Shr:              return ">>";
        case TokenType::Bang:             return "!";
        case TokenType::Question:         return "?";
        case TokenType::At:               return "@";

        // ---- 标点 ----
        case TokenType::LParen:           return "(";
        case TokenType::RParen:           return ")";
        case TokenType::LBrace:           return "{";
        case TokenType::RBrace:           return "}";
        case TokenType::LBracket:         return "[";
        case TokenType::RBracket:         return "]";
        case TokenType::Equal:            return "=";
        case TokenType::Semicolon:        return ";";
        case TokenType::Comma:            return ",";
        case TokenType::Dot:              return ".";
        case TokenType::Colon:            return ":";
        case TokenType::Arrow:            return "=>";
        case TokenType::ArrowRight:       return "->";
        case TokenType::NullCoalescing:   return "??";
        
        case TokenType::END:              return "end of file";
        
        case TokenType::Identifier:       return "identifier";
        case TokenType::Using:            return "using";
        case TokenType::Comment:          return "comment";

        case TokenType::KwInt:            return "int";
        case TokenType::KwString:         return "string";
        case TokenType::KwBool:           return "bool";
        case TokenType::KwChar:           return "char";
        case TokenType::KwVoid:           return "void";
        case TokenType::KwReturn:         return "return";
        case TokenType::KwConst:          return "const";
        case TokenType::KwRef:            return "ref";
        case TokenType::KwAny:            return "any";
        case TokenType::KwRaise:          return "raise";
        case TokenType::KwInt8:           return "int8";
        case TokenType::KwInt16:          return "int16";
        case TokenType::KwInt32:          return "int32";
        case TokenType::KwInt64:          return "int64";
        case TokenType::KwUInt8:          return "uint8";
        case TokenType::KwUInt16:         return "uint16";
        case TokenType::KwUInt32:         return "uint32";
        case TokenType::KwUInt64:         return "uint64";
        case TokenType::KwFloat:          return "float";
        case TokenType::KwDouble:         return "double";
        case TokenType::KwStruct:         return "struct";
        case TokenType::KwTemplate:       return "template";
        case TokenType::KwTypename:       return "typename";
        case TokenType::KwClass:          return "class";
        case TokenType::KwNew:            return "new";
        case TokenType::KwSelf:           return "self";
        case TokenType::KwSuper:          return "super";
        case TokenType::KwStatic:         return "static";
        case TokenType::KwOverride:       return "override";
        case TokenType::KwAbstract:       return "abstract";
        case TokenType::KwPublic:         return "public";
        case TokenType::KwPrivate:        return "private";
        case TokenType::KwProtected:      return "protected";
        case TokenType::KwFor:            return "for";
        case TokenType::KwIf:             return "if";
        case TokenType::KwElse:           return "else";
        case TokenType::KwWhile:          return "while";
        case TokenType::KwDo:             return "do";
        case TokenType::KwForeach:        return "foreach";
        case TokenType::KwIn:             return "in";
        case TokenType::KwNamespace:      return "namespace";
        case TokenType::KwExtern:         return "extern";
        case TokenType::KwList:           return "List";
        case TokenType::KwDict:           return "Dict";
        case TokenType::KwFinalList:      return "FinalList";
        case TokenType::KwFinalDict:      return "FinalDict";
        case TokenType::KwTry:            return "try";
        case TokenType::KwCatch:          return "catch";
        case TokenType::KwFinally:        return "finally";
        case TokenType::KwBreak:          return "break";
        case TokenType::KwContinue:       return "continue";
        case TokenType::KwSwitch:         return "switch";
        case TokenType::KwCase:           return "case";
        case TokenType::KwDefault:        return "default";
        case TokenType::KwWith:           return "with";
        case TokenType::KwSignal:         return "signal";
        case TokenType::KwIs:             return "is";
        case TokenType::KwTypeof:         return "typeof";
        case TokenType::KwEnum:           return "enum";

        case TokenType::True:             return "true";
        case TokenType::False:            return "false";
        case TokenType::StringLiteral:    return "StringLiteral";
        case TokenType::CharLiteral:      return "CharLiteral";
    }
    return "Unknown";
}

class Token{
public:
    TokenType type;
    std::string value;
    int line;
    int col;

    Token(TokenType argType, std::string argValue, int argLine, int argCol = 0)
        : type(argType), value(std::move(argValue)), line(argLine), col(argCol) {}

    std::string typeName(TokenType t) const { return tokenTypeName(t); }
    std::string toString() const { return typeName(type) + '(' + value + ')'; }
};

#endif