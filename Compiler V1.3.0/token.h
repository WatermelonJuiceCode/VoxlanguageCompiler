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
        case TokenType::Plus:          return "Plus";
        case TokenType::PlusPlus:      return "PlusPlus";
        case TokenType::PlusEqual:     return "PlusEqual";
        case TokenType::Sub:           return "Sub";
        case TokenType::SubSub:        return "SubSub";
        case TokenType::SubEqual:      return "SubEqual";
        case TokenType::Mul:           return "Mul";
        case TokenType::Div:           return "Div";
        case TokenType::Mod:           return "Mod";
        case TokenType::MulEqual:      return "MulEqual";
        case TokenType::DivEqual:      return "DivEqual";
        case TokenType::ModEqual:      return "ModEqual";
        case TokenType::Less:          return "Less";
        case TokenType::Greater:       return "Greater";
        case TokenType::LessEqual:     return "LessEqual";
        case TokenType::GreaterEqual:  return "GreaterEqual";
        case TokenType::EqualEqual:    return "EqualEqual";
        case TokenType::NotEqual:      return "NotEqual";
        case TokenType::Amp:           return "Amp";
        case TokenType::AmpAmp:        return "AmpAmp";
        case TokenType::Pipe:          return "Pipe";
        case TokenType::PipePipe:      return "PipePipe";
        case TokenType::Caret:         return "Caret";
        case TokenType::Tilde:         return "Tilde";
        case TokenType::Shl:           return "Shl";
        case TokenType::Shr:           return "Shr";
        case TokenType::Bang:          return "Bang";
        case TokenType::Question:      return "Question";
        case TokenType::At:            return "At";
        case TokenType::LParen:        return "LParen";
        case TokenType::RParen:        return "RParen";
        case TokenType::LBrace:        return "LBrace";
        case TokenType::RBrace:        return "RBrace";
        case TokenType::LBracket:      return "LBracket";
        case TokenType::RBracket:      return "RBracket";
        case TokenType::END:           return "END";

        case TokenType::Identifier:    return "Identifier";
        case TokenType::Equal:         return "Equal";
        case TokenType::Semicolon:     return "Semicolon";
        case TokenType::Comma:         return "Comma";
        case TokenType::Dot:           return "Dot";
        case TokenType::Colon:         return "Colon";
        case TokenType::Arrow:         return "=>";
        case TokenType::ArrowRight:    return "->";
        case TokenType::Using:         return "Using";
        case TokenType::Comment:       return "Comment";

        case TokenType::KwInt:         return "int";
        case TokenType::KwString:      return "string";
        case TokenType::KwBool:        return "bool";
        case TokenType::KwChar:        return "char";
        case TokenType::KwVoid:        return "void";
        case TokenType::KwReturn:      return "return";
        case TokenType::KwConst:       return "const";
        case TokenType::KwRef:         return "ref";
        case TokenType::KwAny:         return "any";
        case TokenType::KwRaise:       return "raise";
        case TokenType::KwInt8:        return "int8";
        case TokenType::KwInt16:       return "int16";
        case TokenType::KwInt32:       return "int32";
        case TokenType::KwInt64:       return "int64";
        case TokenType::KwUInt8:       return "uint8";
        case TokenType::KwUInt16:      return "uint16";
        case TokenType::KwUInt32:      return "uint32";
        case TokenType::KwUInt64:      return "uint64";
        case TokenType::KwFloat:       return "float";
        case TokenType::KwDouble:      return "double";
        case TokenType::KwStruct:      return "struct";
        case TokenType::KwTemplate:    return "template";
        case TokenType::KwTypename:    return "typename";
        case TokenType::KwClass:       return "class";
        case TokenType::KwNew:         return "new";
        case TokenType::KwSelf:        return "self";
        case TokenType::KwSuper:       return "super";
        case TokenType::KwStatic:      return "static";
        case TokenType::KwOverride:    return "override";
        case TokenType::KwAbstract:    return "abstract";
        case TokenType::KwPublic:      return "public";
        case TokenType::KwPrivate:     return "private";
        case TokenType::KwProtected:   return "protected";
        case TokenType::KwFor:         return "for";
        case TokenType::KwIf:          return "if";
        case TokenType::KwElse:        return "else";
        case TokenType::KwWhile:       return "while";
        case TokenType::KwDo:          return "do";
        case TokenType::KwForeach:     return "foreach";
        case TokenType::KwIn:          return "in";
        case TokenType::KwNamespace:   return "namespace";
        case TokenType::KwExtern:      return "extern";
        case TokenType::KwList:        return "List";
        case TokenType::KwDict:        return "Dict";
        case TokenType::KwFinalList:   return "FinalList";
        case TokenType::KwFinalDict:   return "FinalDict";
        case TokenType::KwTry:         return "try";
        case TokenType::KwCatch:       return "catch";
        case TokenType::KwFinally:     return "finally";
        case TokenType::KwBreak:       return "break";
        case TokenType::KwContinue:    return "continue";
        case TokenType::KwSwitch:      return "switch";
        case TokenType::KwCase:        return "case";
        case TokenType::KwDefault:     return "default";
        case TokenType::KwWith:        return "with";
        case TokenType::KwIs:          return "is";
        case TokenType::KwTypeof:      return "typeof";
        case TokenType::KwEnum:        return "enum";

        case TokenType::True:          return "true";
        case TokenType::False:         return "false";
        case TokenType::StringLiteral: return "StringLiteral";
        case TokenType::CharLiteral:   return "CharLiteral";
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