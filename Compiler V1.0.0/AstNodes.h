#ifndef AST_NODES_H
#define AST_NODES_H

#include <cctype>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <set>
#include <optional>

#include "token.h"
#include "variables.h"

using AstNodePtr = std::shared_ptr<class AstNode>;

class AstNode {
public:
    virtual ~AstNode() = default;
    virtual std::string toString() const = 0;
    virtual std::string toC() const = 0;
    virtual std::string toC(bool /*skipIncludes*/) const { return toC(); }
    virtual bool needsStringInclude() const { return false; }
    virtual bool isHeaderInclude() const { return false; }
    virtual std::string headerIncludeLine() const { return ""; }
    virtual bool isTopLevelDefinition() const { return false; }

    // ★ 告诉调用方这是不是一个 class（用于 NamespaceNode 里的前向声明）
    virtual bool isClassLike(std::string& outName) const { return false; }

    // ★ 返回前向声明（含可能的 template 头）
    virtual std::string forwardDecl() const { return ""; }
};

inline std::string escapeCString(const std::string& s){
    std::string r;
    for (unsigned char c : s){
        switch (c){
        case '"':  r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n";  break;
        case '\r': r += "\\r";  break;
        case '\t': r += "\\t";  break;
        case '\a': r += "\\a";  break;
        case '\b': r += "\\b";  break;
        case '\f': r += "\\f";  break;
        case '\v': r += "\\v";  break;
        case '\0': r += "\\0";  break;
        default:
            r += static_cast<char>(c);
            break;
        }
    }
    return r;
}

// ===================== mapTypeToC =====================
inline std::string mapTypeToC(const std::string& typeName){
    // ---- 基础类型：用 C++ 原生类型 ----
    if (typeName == "bool")   return "bool";
    if (typeName == "char")   return "char";
    if (typeName == "int")    return "int";
    if (typeName == "void")   return "void";
    if (typeName == "any")    return "auto";
    if (typeName == "float")  return "float";
    if (typeName == "double") return "double";

    // ---- Vox 自己定义的类型 ----
    if (typeName == "string") return "vox::String";

    // ---- 定长整型 ----
    if (typeName == "int8")   return "int8_t";
    if (typeName == "int16")  return "int16_t";
    if (typeName == "int32")  return "int32_t";
    if (typeName == "int64")  return "int64_t";
    if (typeName == "uint8")  return "uint8_t";
    if (typeName == "uint16") return "uint16_t";
    if (typeName == "uint32") return "uint32_t";
    if (typeName == "uint64") return "uint64_t";

    // ---- 函数类型：ReturnType(ParamType1, ParamType2, ...) ----
    {
        auto open = typeName.find('(');
        if (open != std::string::npos && typeName.back() == ')'){
            std::string retType = typeName.substr(0, open);
            std::string paramsStr = typeName.substr(open + 1, typeName.size() - open - 2);

            std::string cRet = mapTypeToC(retType);

            // 分割顶层逗号
            std::vector<std::string> params;
            {
                int depth = 0;
                std::size_t start = 0;
                for (std::size_t i = 0; i < paramsStr.size(); ++i){
                    char c = paramsStr[i];
                    if (c == '<' || c == '(') depth++;
                    else if (c == '>' || c == ')') depth--;
                    else if (c == ',' && depth == 0){
                        params.push_back(paramsStr.substr(start, i - start));
                        start = i + 1;
                    }
                }
                if (start < paramsStr.size()){
                    params.push_back(paramsStr.substr(start));
                }
            }

            std::string cParams;
            for (std::size_t i = 0; i < params.size(); ++i){
                if (i > 0) cParams += ", ";
                std::string p = params[i];
                while (!p.empty() && (p.front() == ' ' || p.front() == '\t')) p.erase(p.begin());
                while (!p.empty() && (p.back()  == ' ' || p.back()  == '\t')) p.pop_back();
                cParams += mapTypeToC(p);
            }

            return "std::function<" + cRet + "(" + cParams + ")>";
        }
    }

    auto startsWith = [](const std::string& s, const std::string& p){
        return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
    };
    auto trim = [](std::string s){
        while (!s.empty() && s.front() == ' ') s.erase(s.begin());
        while (!s.empty() && s.back()  == ' ') s.pop_back();
        return s;
    };
    auto splitTop = [](const std::string& s, char delim){
        std::vector<std::string> parts;
        int depth = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i < s.size(); ++i){
            if (s[i] == '<') depth++;
            else if (s[i] == '>') depth--;
            else if (s[i] == delim && depth == 0){
                parts.push_back(s.substr(start, i - start));
                start = i + 1;
            }
        }
        parts.push_back(s.substr(start));
        return parts;
    };

    // ---- Vox 容器类型 ----
    if (startsWith(typeName, "FinalList<") && typeName.back() == '>'){
        std::string inner = typeName.substr(10, typeName.size() - 11);
        return "vox::FinalList<" + mapTypeToC(trim(inner)) + ">";
    }
    if (startsWith(typeName, "List<") && typeName.back() == '>'){
        std::string inner = typeName.substr(5, typeName.size() - 6);
        return "vox::List<" + mapTypeToC(trim(inner)) + ">";
    }
    if (startsWith(typeName, "FinalDict<") && typeName.back() == '>'){
        std::string inner = typeName.substr(10, typeName.size() - 11);
        auto parts = splitTop(inner, ',');
        if (parts.size() == 2){
            return "vox::FinalDict<" + mapTypeToC(trim(parts[0])) + ", "
                                     + mapTypeToC(trim(parts[1])) + ">";
        }
    }
    if (startsWith(typeName, "Dict<") && typeName.back() == '>'){
        std::string inner = typeName.substr(5, typeName.size() - 6);
        auto parts = splitTop(inner, ',');
        if (parts.size() == 2){
            return "vox::Dict<" + mapTypeToC(trim(parts[0])) + ", "
                               + mapTypeToC(trim(parts[1])) + ">";
        }
    }

    // ★ 通用泛型：Foo<A, B, ...> → Foo<mapTypeToC(A), mapTypeToC(B), ...>
    if (typeName.back() == '>'){
        auto open = typeName.find('<');
        if (open != std::string::npos && open > 0){
            std::string base = typeName.substr(0, open);
            std::string inner = typeName.substr(open + 1, typeName.size() - open - 2);
            auto parts = splitTop(inner, ',');
            std::string cInner;
            for (std::size_t i = 0; i < parts.size(); ++i){
                if (i > 0) cInner += ", ";
                cInner += mapTypeToC(trim(parts[i]));
            }
            return base + "<" + cInner + ">";
        }
    }
    return typeName;
}

// ===================== 叶子节点 =====================

class NumberNode : public AstNode{
public:
    int64_t value;
    std::string rawText;                      // ★ 原始字面量文本

    explicit NumberNode(int64_t argValue, std::string raw = "")
        : value(argValue), rawText(std::move(raw)) {}

    std::string toString() const override { return std::to_string(value); }

    std::string toC() const override {
        // 保留原始字面量（十六进制 / 二进制 / 八进制）
        if (!rawText.empty()) return rawText;
        return std::to_string(value);
    }
};

class BoolNode : public AstNode{
public:
    bool value;
    explicit BoolNode(bool argValue) : value(argValue) {}
    std::string toString() const override { return value ? "true" : "false"; }
    std::string toC() const override { return value ? "true" : "false"; }
};

class NullNode : public AstNode {
public:
    std::string toString() const override { return "null"; }
    std::string toC() const override { return "nullptr"; }
};

// 根据类型生成"空值"的字面量
inline std::string nullAsType(const std::string& typeName){
    if (typeName == "string") return "\"\"";
    if (typeName == "bool")   return "false";
    if (typeName == "float")  return "0.0f";
    if (typeName == "double") return "0.0";
    if (typeName == "char")   return "'\\0'";
    if (typeName == "int" || typeName == "int8" || typeName == "int16" ||
        typeName == "int32" || typeName == "int64" ||
        typeName == "uint8" || typeName == "uint16" ||
        typeName == "uint32" || typeName == "uint64")
        return "0";
    // 自定义类：默认构造
    return mapTypeToC(typeName) + "()";
}

class StringNode : public AstNode{
public:
    std::string value;
    explicit StringNode(std::string argValue) : value(std::move(argValue)) {}
    std::string toString() const override { return value; }
    std::string toC() const override { return "\"" + escapeCString(value) + "\""; }
};

class CharNode : public AstNode{
public:
    char value;
    explicit CharNode(char argValue) : value(argValue) {}
    std::string toString() const override { return std::string(1, value); }
    std::string toC() const override {
        std::string escaped;
        switch (value){
        case '\'': escaped = "\\'";  break;
        case '\\': escaped = "\\\\"; break;
        case '\n': escaped = "\\n";  break;
        case '\r': escaped = "\\r";  break;
        case '\t': escaped = "\\t";  break;
        case '\a': escaped = "\\a";  break;
        case '\b': escaped = "\\b";  break;
        case '\f': escaped = "\\f";  break;
        case '\v': escaped = "\\v";  break;
        case '\0': escaped = "\\0";  break;
        default:
            escaped = std::string(1, value);
            break;
        }
        return "'" + escaped + "'";
    }
};

class FloatNode : public AstNode{
public:
    double value;
    bool isFloat32;                    // true = float, false = double
    explicit FloatNode(double v, bool f32 = false)
        : value(v), isFloat32(f32) {}

    std::string toString() const override {
        std::string s = std::to_string(value);
        return isFloat32 ? s + "f" : s;
    }
    std::string toC() const override {
        // 保证浮点字面量一定带小数点或 e，避免生成 C++ 里的整数
        std::string s = std::to_string(value);
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos){
            s += ".0";
        }
        return isFloat32 ? s + "f" : s;
    }
};

class VariableNode : public AstNode{
public:
    std::string name;
    int line = 0;

    explicit VariableNode(std::string argName, int srcLine = 0)
        : name(std::move(argName)), line(srcLine) {}

    std::string toString() const override { return name; }
    std::string toC() const override { return name; }
};

class NamedArgNode : public AstNode {
public:
    std::string name;
    AstNodePtr value;

    NamedArgNode(std::string n, AstNodePtr v)
        : name(std::move(n)), value(std::move(v)) {}

    std::string toString() const override {
        return name + "=" + (value ? value->toString() : std::string());
    }
    // 正常情况 CallNode 重排后不会出现 NamedArgNode，
    // 保留一个兜底实现（仅输出值）
    std::string toC() const override {
        return value ? value->toC() : std::string();
    }
    bool needsStringInclude() const override {
        return value ? value->needsStringInclude() : false;
    }
};

class SelfNode : public AstNode {
public:
    std::string toString() const override { return "Self"; }
    std::string toC() const override { return "(*this)"; }
};

class MemberAccessNode : public AstNode {
public:
    AstNodePtr object;
    std::string member;
    bool namespaceAccess;

    MemberAccessNode(AstNodePtr obj, std::string mem, bool ns = false)
        : object(std::move(obj)), member(std::move(mem)), namespaceAccess(ns) {}

    std::string toString() const override { return object->toString() + "." + member; }

    std::string toC() const override {
        // 完全信任 Parser 设置的 namespaceAccess：
        //   - Parser 通过 _namespaceAliases / _knownNamespaces 判断是不是命名空间
        //   - 大写开头的变量不应该被自动当作类型名
        if (namespaceAccess){
            return object->toC() + "::" + member;
        }
        return object->toC() + "." + member;
    }

    bool needsStringInclude() const override { return object->needsStringInclude(); }
};

class IndexNode : public AstNode {
public:
    AstNodePtr object;
    AstNodePtr index;
    int line = 0;

    IndexNode(AstNodePtr o, AstNodePtr i, int srcLine = 0)
        : object(std::move(o)), index(std::move(i)), line(srcLine) {}

    std::string toString() const override {
        return object->toString() + "[" + index->toString() + "]";
    }
    std::string toC() const override {
        return "vox::vox_index(" + object->toC() + ", " + index->toC() + ")";
    }
    bool needsStringInclude() const override {
        return object->needsStringInclude() || index->needsStringInclude();
    }
};

// ===================== 赋值表达式 =====================
struct AssignPair {
    AstNodePtr target;
    AstNodePtr value;
};

class AssignExprNode : public AstNode {
public:
    std::vector<AssignPair> assigns;
    AstNodePtr finalExpr;

    AssignExprNode(std::vector<AssignPair> a, AstNodePtr f)
        : assigns(std::move(a)), finalExpr(std::move(f)) {}

    std::string toString() const override {
        std::string r = "AssignExpr(";
        for (std::size_t i = 0; i < assigns.size(); ++i){
            if (i > 0) r += " ; ";
            r += assigns[i].target->toString() + " = " + assigns[i].value->toString();
        }
        r += " ; " + (finalExpr ? finalExpr->toString() : std::string()) + ")";
        return r;
    }

    std::string toC() const override {
        return (finalExpr ? finalExpr->toC() : "");
    }

    bool needsStringInclude() const override {
        for (const auto& p : assigns){
            if (p.target->needsStringInclude()) return true;
            if (p.value && p.value->needsStringInclude()) return true;
        }
        return finalExpr ? finalExpr->needsStringInclude() : false;
    }
};

class UnaryNode : public AstNode {
public:
    TokenType op;
    AstNodePtr operand;
    int line = 0;

    UnaryNode(TokenType o, AstNodePtr e, int srcLine = 0)
        : op(o), operand(std::move(e)), line(srcLine) {}

    std::string toString() const override {
        return "Unary(" + tokenTypeName(op) + " " + operand->toString() + ")";
    }

    std::string toC() const override {
        if (op == TokenType::Sub)  return "vox::vox_neg(" + operand->toC() + ")";
        if (op == TokenType::Bang) return "vox::vox_not(" + operand->toC() + ")";

        std::string opStr;
        switch (op){
        case TokenType::Tilde: opStr = "~"; break;
        case TokenType::Plus:  opStr = "+"; break;
        default:               opStr = "?"; break;
        }
        return "(" + opStr + operand->toC() + ")";
    }

    bool needsStringInclude() const override {
        return operand ? operand->needsStringInclude() : false;
    }
};

class CallNode : public AstNode {
public:
    AstNodePtr callee;
    std::vector<AstNodePtr> args;
    int line = 0;
    bool userInvoke = false;   // 是否走 _call_

    CallNode(AstNodePtr c, std::vector<AstNodePtr> a, int srcLine = 0)
        : callee(std::move(c)), args(std::move(a)), line(srcLine) {}

    std::string toString() const override {
        std::string r = "Call(" + callee->toString();
        for (std::size_t i = 0; i < args.size(); ++i){
            r += (i == 0 ? "(" : ", ");
            r += args[i]->toString();
        }
        r += ")";
        return r;
    }

    std::string toC() const override {
        std::string pre;
        std::vector<std::string> realArgs;

        for (const auto& a : args){
            if (auto ae = std::dynamic_pointer_cast<AssignExprNode>(a)){
                for (const auto& p : ae->assigns){
                    pre += p.target->toC() + " = " + p.value->toC() + ";\n";
                }
                realArgs.push_back(ae->finalExpr ? ae->finalExpr->toC() : "");
            } else {
                realArgs.push_back(a->toC());
            }
        }

        std::string call;
        if (userInvoke){
            call = "vox::vox_invoke(" + callee->toC();
            for (const auto& a : realArgs) call += ", " + a;
            call += ")";
        } else {
            call = callee->toC() + "(";
            for (std::size_t i = 0; i < realArgs.size(); ++i){
                if (i > 0) call += ", ";
                call += realArgs[i];
            }
            call += ")";
        }

        if (pre.empty()) return call;
        return "{ " + pre + call + "; }";
    }

    bool needsStringInclude() const override {
        if (callee->needsStringInclude()) return true;
        for (const auto& a : args){
            if (a->needsStringInclude()) return true;
        }
        return false;
    }
};

// ===================== 类型检测节点 =====================

class TypeofNode : public AstNode {
public:
    AstNodePtr expr;

    explicit TypeofNode(AstNodePtr e) : expr(std::move(e)) {}

    std::string toString() const override {
        return "Typeof(" + expr->toString() + ")";
    }

    std::string toC() const override {
        return "vox::vox_typeof(" + expr->toC() + ")";
    }

    bool needsStringInclude() const override { return true; }
};

class IsNode : public AstNode {
public:
    AstNodePtr expr;
    std::string targetType;
    int line = 0;

    IsNode(AstNodePtr e, std::string t, int srcLine = 0)
        : expr(std::move(e)), targetType(std::move(t)), line(srcLine) {}

    std::string toString() const override {
        return "Is(" + expr->toString() + ", " + targetType + ")";
    }
    std::string toC() const override {
        std::string cleanName = targetType;
        auto pos = cleanName.rfind("::");
        if (pos != std::string::npos) cleanName = cleanName.substr(pos + 2);
        return "vox::vox_is_type(" + expr->toC() + ", \"" + cleanName + "\")";
    }
    bool needsStringInclude() const override { return true; }
};

struct EnumMember {
    std::string name;
    int value;
    bool hasExplicitValue;
};

class EnumNode : public AstNode {
public:
    std::string name;
    std::vector<EnumMember> members;

    EnumNode(std::string n, std::vector<EnumMember> m)
        : name(std::move(n)), members(std::move(m)) {}

    std::string toString() const override {
        std::string r = "Enum(" + name + "){";
        for (std::size_t i = 0; i < members.size(); ++i){
            if (i > 0) r += ", ";
            r += members[i].name + "=" + std::to_string(members[i].value);
        }
        r += "}";
        return r;
    }

    std::string toC() const override {
        std::string r = "enum class " + name + " : int {\n";
        for (std::size_t i = 0; i < members.size(); ++i){
            r += "    " + members[i].name;
            // 显式值才写 = N，否则让 C++ 自动递增
            if (members[i].hasExplicitValue){
                r += " = " + std::to_string(members[i].value);
            }
            r += ",\n";
        }
        r += "};\n";

        // 类型名 + 字符串名查询
        r += "inline vox::String vox_type_name_of(const " + name + "&) { return \"" + name + "\"; }\n";
        r += "inline vox::String vox_to_string(const " + name + "& v) {\n";
        r += "    switch (v) {\n";
        for (const auto& m : members){
            r += "    case " + name + "::" + m.name + ": return \"" + m.name + "\";\n";
        }
        r += "    default: return \"?\";\n";
        r += "    }\n";
        r += "}\n";

        return r;
    }

    bool needsStringInclude() const override { return true; }
    bool isTopLevelDefinition() const override { return true; }
};

class NewNode : public AstNode {
public:
    std::string className;
    std::vector<AstNodePtr> args;
    int line = 0;

    NewNode(std::string cls, std::vector<AstNodePtr> a, int srcLine = 0)
        : className(std::move(cls)), args(std::move(a)), line(srcLine) {}

    std::string toString() const override {
        std::string r = "New(" + className + "(";
        for (std::size_t i = 0; i < args.size(); ++i){
            if (i > 0) r += ", ";
            r += args[i]->toString();
        }
        r += "))";
        return r;
    }
    std::string toC() const override {
        std::string r = mapTypeToC(className) + "(";   // ★ 走 mapTypeToC
        for (std::size_t i = 0; i < args.size(); ++i){
            if (i > 0) r += ", ";
            r += args[i]->toC();
        }
        r += ")";
        return r;
    }
};

class CastNode : public AstNode {
public:
    std::string targetType;
    AstNodePtr expr;
    AstNodePtr extraArg;
    int line = 0;

    CastNode(std::string t, AstNodePtr e, AstNodePtr extra = nullptr, int srcLine = 0)
        : targetType(std::move(t)), expr(std::move(e)),
          extraArg(std::move(extra)), line(srcLine) {}

    std::string toString() const override {
        return "Cast(" + targetType + ", " + expr->toString()
             + (extraArg ? ", " + extraArg->toString() : "") + ")";
    }

    std::string toC() const override {
        // string(count, char) 特例
        if (targetType == "string" && extraArg){
            return "vox::String(" + expr->toC() + ", " + extraArg->toC() + ")";
        }

        if (targetType == "string") return "vox::vox_str(" + expr->toC() + ")";
        if (targetType == "int")    return "vox::vox_cast_int(" + expr->toC() + ")";
        if (targetType == "bool")   return "vox::vox_cast_bool(" + expr->toC() + ")";
        if (targetType == "char")   return "vox::vox_cast_char(" + expr->toC() + ")";
        if (targetType == "float")  return "vox::vox_cast_float(" + expr->toC() + ")";
        if (targetType == "double") return "vox::vox_cast_double(" + expr->toC() + ")";

        // 其它类型（定长整型、用户类之间转换）→ static_cast
        return "static_cast<" + mapTypeToC(targetType) + ">(" + expr->toC() + ")";
    }

    bool needsStringInclude() const override {
        if (expr->needsStringInclude()) return true;
        if (extraArg && extraArg->needsStringInclude()) return true;
        return false;
    }
};

class ExpressionStatementNode : public AstNode {
public:
    AstNodePtr expr;
    bool hasTrace = false;
    std::string traceFile;
    std::string traceSrcLine;
    int traceLine = 0;
    int traceColStart = 0;
    int traceColLen = 0;

    explicit ExpressionStatementNode(AstNodePtr e) : expr(std::move(e)) {}

    std::string toString() const override {
        return "Expr(" + (expr ? expr->toString() : "") + ")";
    }

    std::string toC() const override {
        if (!expr) return ";";
        std::string code = expr->toC();
        if (!code.empty() && code.back() == ';'){
            // 已有分号（不常见），直接返回
            if (!hasTrace) return code;
        }
        if (hasTrace){
            std::string call = code;
            if (!call.empty() && call.back() == ';') call.pop_back();
            return "{ vox::FrameGuard _vfg(\""
                + escapeCString(traceFile) + "\", "
                + std::to_string(traceLine) + ", \""
                + escapeCString(traceSrcLine) + "\", "
                + std::to_string(traceColStart) + ", "
                + std::to_string(traceColLen) + ", -1, 0, __func__); "
                + call + "; }";
            }
            return code + ";";
        }
        
        bool needsStringInclude() const override {
            return expr ? expr->needsStringInclude() : false;
        }
    };
    
    struct Attribute {
        std::string name;                        // 例："type"
        std::vector<std::string> args;           // 例：{"string"}，存字符串形式
    int line = 0;
};

class BinaryNode : public AstNode {
    public:
    AstNodePtr left;
    TokenType op;
    AstNodePtr right;
    int line = 0;
    
    BinaryNode(AstNodePtr argLeft, TokenType argOp, AstNodePtr argRight,
               int srcLine = 0)
        : left(std::move(argLeft)), op(argOp), right(std::move(argRight)),
        line(srcLine) {}
        
        std::string toString() const override {
            return "(" + left->toString() + " " + tokenTypeName(op) + " " + right->toString() + ")";
        }
        std::string toC() const override {
            // ★ == null 特判
            if (op == TokenType::EqualEqual && std::dynamic_pointer_cast<NullNode>(right)) {
                return "vox::vox_is_null(" + left->toC() + ")";
            }
            if (op == TokenType::NotEqual && std::dynamic_pointer_cast<NullNode>(right)) {
                return "(!vox::vox_is_null(" + left->toC() + "))";
            }
            if (op == TokenType::EqualEqual && std::dynamic_pointer_cast<NullNode>(left)) {
                return "vox::vox_is_null(" + right->toC() + ")";
            }
            if (op == TokenType::NotEqual && std::dynamic_pointer_cast<NullNode>(left)) {
                return "(!vox::vox_is_null(" + right->toC() + "))";
            }
        // ---- 除零检查 ----
        if (op == TokenType::Div){
            return "vox::vox_div(" + left->toC() + ", " + right->toC() + ")";
        }
        if (op == TokenType::Mod){
            return "vox::vox_mod(" + left->toC() + ", " + right->toC() + ")";
        }

        // ---- 字符串拼接：字面量自动包装成 vox::String ----
        if (op == TokenType::Plus){
            bool lStr = std::dynamic_pointer_cast<StringNode>(left)  != nullptr;
            bool rStr = std::dynamic_pointer_cast<StringNode>(right) != nullptr;
            std::string l = left->toC();
            std::string r = right->toC();
            if (lStr && rStr){
                return "vox::String(" + l + ") + " + r;
            }
            if (lStr){
                // ★ 右侧显式 vox_str()，避免 C++ 隐式转换（如 Rect 的 operator bool）引发歧义
                return "vox::String(" + l + ") + vox::vox_str(" + r + ")";
            }
            if (rStr){
                return "vox::vox_str(" + l + ") + vox::String(" + r + ")";
            }
        }

        std::string opStr;
        switch (op){
        case TokenType::Plus:         opStr = "+";  break;
        case TokenType::Sub:          opStr = "-";  break;
        case TokenType::Mul:          opStr = "*";  break;
        case TokenType::Div:          opStr = "/";  break;
        case TokenType::Mod:          opStr = "%";  break; 
        case TokenType::Less:         opStr = "<";  break;
        case TokenType::Greater:      opStr = ">";  break;
        case TokenType::LessEqual:    opStr = "<="; break;
        case TokenType::GreaterEqual: opStr = ">="; break;
        case TokenType::EqualEqual:   opStr = "=="; break;
        case TokenType::NotEqual:     opStr = "!="; break;
        case TokenType::Shl:          opStr = "<<"; break;
        case TokenType::Shr:          opStr = ">>"; break;
        case TokenType::Amp:          opStr = "&";  break;
        case TokenType::Pipe:         opStr = "|";  break;
        case TokenType::Caret:        opStr = "^";  break;
        case TokenType::AmpAmp:       opStr = "&&"; break;
        case TokenType::PipePipe:     opStr = "||"; break;
        default:                      opStr = "?";  break;
        }
        return "(" + left->toC() + " " + opStr + " " + right->toC() + ")";
    }
};

class VariableDeclNode : public AstNode {
public:
    std::string typeName;
    std::vector<std::string> names;
    std::vector<AstNodePtr> values;
    bool isConst;
    bool isGlobal = false;      // ← 顶层声明
    bool isHeapCaptured = false;   // ★ 被 ref lambda 捕获，需堆分配
    int line = 0;


    VariableDeclNode(std::string argType,
                     std::vector<std::string> argNames,
                     std::vector<AstNodePtr> argValues,
                     bool argIsConst = false, 
                     int srcLine = 0)
        : typeName(std::move(argType)),
          names(std::move(argNames)),
          values(std::move(argValues)),
          isConst(argIsConst), 
          line(srcLine) {}

    std::string toString() const override {
        std::string result = isConst ? "ConstVariable(Type(" : "Variable(Type(";
        result += typeName + "),";
        if (names.size() == 1){
            result += "Name(" + names[0] + "),Value(";
            result += valueToString(0);
            result += ")";
        } else {
            result += "Names(";
            for (std::size_t i = 0; i < names.size(); ++i){
                if (i > 0) result += ",";
                result += names[i];
            }
            result += "),Values(";
            for (std::size_t i = 0; i < values.size(); ++i){
                if (i > 0) result += ",";
                result += valueToString(i);
            }
            result += ")";
        }
        result += ")";
        return result;
    }

        std::string toC() const override {
        std::string baseType = mapTypeToC(typeName);
        std::string result;

        if (isConst) result += "const ";

        if (isHeapCaptured){
            // ★ 堆分配：vox::VoxHeap<T> name(init);
            result += "vox::VoxHeap<" + baseType + ">";
        } else {
            result += baseType;
        }

        for (std::size_t i = 0; i < names.size(); ++i){
            if (i == 0) result += " ";
            else        result += ", ";
            result += names[i];
            if (i < values.size() && values[i]){
                if (isHeapCaptured){
                    result += "(" + values[i]->toC() + ")";
                } else {
                    result += " = " + values[i]->toC();
                }
            }
        }
        result += ";";
        return result;
    }
    bool needsStringInclude() const override {
        if (typeName == "string") return true;
        for (const auto& v : values){
            if (v && v->needsStringInclude()) return true;
        }
        return false;
    }

    // ← 新增：告诉 ProgramNode 这个节点应该放在 main 外面
    bool isTopLevelDefinition() const override { return isGlobal; }

private:
    std::string valueToString(std::size_t index) const {
        if (index >= values.size() || !values[index]) return " ";
        return values[index]->toString();
    }
};

// ============ 参数约束 ============
struct Constraint {
    std::string className;              // "vox_mod_Code_edit::Range"
    std::vector<AstNodePtr> args;       // [-10, 10]

    std::string toCCheck(const std::string& varName) const {
        std::string r = className + "(" + varName;
        for (const auto& a : args) r += ", " + a->toC();
        r += ");";
        return r;
    }
    bool needsStringInclude() const {
        for (const auto& a : args) if (a && a->needsStringInclude()) return true;
        return false;
    }
};

class AssignmentNode : public AstNode {
public:
    std::string name;
    AstNodePtr target;
    AstNodePtr value;
    int line = 0;
    bool isCompound = false;
    std::string compoundOp;
    std::optional<Constraint> constraint;    // ★

    AssignmentNode(std::string argName, AstNodePtr argValue, int srcLine = 0)
        : name(std::move(argName)), target(nullptr),
          value(std::move(argValue)), line(srcLine) {}

    AssignmentNode(AstNodePtr argTarget, AstNodePtr argValue, int srcLine = 0)
        : name(""), target(std::move(argTarget)),
          value(std::move(argValue)), line(srcLine) {}

    std::string toString() const override {
        return "Assign(" + name + " = " + (value ? value->toString() : std::string()) + ")";
    }
    
    std::string toC() const override {
        std::string lhs = target ? target->toC() : name;
        if (constraint.has_value()){
            std::string tmp = "_vox_tmp_" + std::to_string(line) + "_" + name;
            std::string rhs;
            if (isCompound && !compoundOp.empty()){
                // 复合：tmp = lhs; tmp op= value;
                rhs = "{ auto " + tmp + " = " + lhs + "; "
                    + tmp + " " + compoundOp + " " + (value ? value->toC() : "0")
                    + "; " + constraint->toCCheck(tmp)
                    + " " + lhs + " = " + tmp + "; }";
            } else {
                rhs = "{ auto " + tmp + " = " + (value ? value->toC() : "0") + "; "
                    + constraint->toCCheck(tmp)
                    + " " + lhs + " = " + tmp + "; }";
            }
            return rhs;
        }
        if (isCompound && !compoundOp.empty()){
            return lhs + " " + compoundOp + " " + (value ? value->toC() : "") + ";";
        }
        return lhs + " = " + (value ? value->toC() : "") + ";";
    }

    bool needsStringInclude() const override {
        if (value && value->needsStringInclude()) return true;
        if (constraint.has_value() && constraint->needsStringInclude()) return true;
        return false;
    }
};

class ImportNode : public AstNode {
public:
    std::string moduleName;
    std::string resolvedPath;
    bool local;
    bool importAll;

    ImportNode(std::string argModuleName,
               std::string argResolvedPath,
               bool argLocal,
               bool argImportAll = false)
        : moduleName(std::move(argModuleName)),
          resolvedPath(std::move(argResolvedPath)),
          local(argLocal),
          importAll(argImportAll) {}

    std::string toString() const override {
        return "Import(" + moduleName + (importAll ? ".*" : "") + " -> " + resolvedPath + ")";
    }
    std::string toC() const override { return ""; }
    bool isLocal() const { return local; }
    bool isHeaderInclude() const override { return !local; }

    std::string headerIncludeLine() const override {
        std::string r = "#include \"" + resolvedPath + "\"";
        if (importAll){
            std::string ns = moduleName;
            auto dot = ns.rfind('.');
            if (dot != std::string::npos) ns = ns.substr(dot + 1);
            r += "\nusing namespace " + ns + ";";
        }
        return r;
    }
};

class ListLiteralNode : public AstNode {
public:
    std::vector<AstNodePtr> items;
    explicit ListLiteralNode(std::vector<AstNodePtr> i) : items(std::move(i)) {}
    std::string toString() const override {
        std::string r = "[";
        for (std::size_t i = 0; i < items.size(); ++i){
            if (i > 0) r += ", ";
            r += items[i]->toString();
        }
        r += "]";
        return r;
    }
    std::string toC() const override {
        std::string r = "{";
        for (std::size_t i = 0; i < items.size(); ++i){
            if (i > 0) r += ", ";
            r += items[i]->toC();
        }
        r += "}";
        return r;
    }
    bool needsStringInclude() const override {
        for (const auto& x : items) if (x->needsStringInclude()) return true;
        return false;
    }
};

class ArrayLiteralNode : public AstNode {
public:
    std::vector<AstNodePtr> items;
    explicit ArrayLiteralNode(std::vector<AstNodePtr> i) : items(std::move(i)) {}
    std::string toString() const override {
        std::string r = "Array(";
        for (std::size_t i = 0; i < items.size(); ++i){
            if (i > 0) r += ", ";
            r += items[i]->toString();
        }
        r += ")";
        return r;
    }
    std::string toC() const override {
        std::string r = "{";
        for (std::size_t i = 0; i < items.size(); ++i){
            if (i > 0) r += ", ";
            r += items[i]->toC();
        }
        r += "}";
        return r;
    }
    bool needsStringInclude() const override {
        for (const auto& x : items) if (x->needsStringInclude()) return true;
        return false;
    }
};

class DictLiteralNode : public AstNode {
public:
    std::vector<AstNodePtr> keys;
    std::vector<AstNodePtr> values;

    DictLiteralNode(std::vector<AstNodePtr> k, std::vector<AstNodePtr> v)
        : keys(std::move(k)), values(std::move(v)) {}

    std::string toString() const override {
        std::string r = "{";
        for (std::size_t i = 0; i < keys.size(); ++i){
            if (i > 0) r += ", ";
            r += keys[i]->toString() + " : " + values[i]->toString();
        }
        r += "}";
        return r;
    }
    std::string toC() const override {
        std::string r = "{";
        for (std::size_t i = 0; i < keys.size(); ++i){
            if (i > 0) r += ", ";
            r += "{" + keys[i]->toC() + ", " + values[i]->toC() + "}";
        }
        r += "}";
        return r;
    }
    bool needsStringInclude() const override {
        for (const auto& k : keys) if (k->needsStringInclude()) return true;
        for (const auto& v : values) if (v->needsStringInclude()) return true;
        return false;
    }
};


struct Param {
    std::string type;
    std::string name;
    bool isRef = false;
    bool isVariadic = false;
    bool afterSlash = false;
    bool isPosOnly = false;
    bool isKwOnly = false;
    AstNodePtr defaultValue;
    std::optional<Constraint> constraint;    // ★
};

class BlockNode : public AstNode {
public:
    std::vector<AstNodePtr> statements;
    BlockNode() = default;
    void add(AstNodePtr node) { statements.push_back(std::move(node)); }
    std::string toString() const override {
        std::string result;
        for (std::size_t i = 0; i < statements.size(); ++i){
            result += statements[i]->toString();
            if (i + 1 < statements.size()) result += "\n";
        }
        return result;
    }
    std::string toC() const override {
        std::string result;
        for (const auto& stmt : statements){
            result += stmt->toC();
            result += "\n";
        }
        return result;
    }
    bool needsStringInclude() const override {
        for (const auto& s : statements){
            if (s->needsStringInclude()) return true;
        }
        return false;
    }
};

// ===================== Lambda =====================
struct LambdaParam {
    std::string type;
    std::string name;
};

class LambdaNode : public AstNode {
public:
    std::string returnType;
    std::vector<LambdaParam> params;
    std::shared_ptr<BlockNode> body;
    bool byRef;
    int line = 0;

    LambdaNode(std::string rt, std::vector<LambdaParam> p,
               std::shared_ptr<BlockNode> b, bool refCapture = false,
               int srcLine = 0)
        : returnType(std::move(rt)), params(std::move(p)),
          body(std::move(b)), byRef(refCapture), line(srcLine) {}

    std::string toString() const override {
        std::string r = "Lambda(" + returnType + "(";
        for (std::size_t i = 0; i < params.size(); ++i){
            if (i > 0) r += ", ";
            r += params[i].type + " " + params[i].name;
        }
        r += ")" + std::string(byRef ? " ref" : "") + " => { "
           + (body ? body->toString() : "") + " })";
        return r;
    }

    std::string toC() const override {
        std::string r = std::string("[") + (byRef ? "&" : "=") + "](";
        for (std::size_t i = 0; i < params.size(); ++i){
            if (i > 0) r += ", ";
            r += mapTypeToC(params[i].type) + " " + params[i].name;
        }
        r += ") -> " + mapTypeToC(returnType) + " {\n";
        if (body) r += body->toC();
        r += "}";
        return r;
    }

    bool needsStringInclude() const override { return true; }
};

class NestedFunctionNode : public AstNode {
public:
    std::string returnType;
    std::string name;
    std::vector<Param> params;
    std::shared_ptr<BlockNode> body;
    bool byRef;

    NestedFunctionNode(std::string rt, std::string n,
                       std::vector<Param> p,
                       std::shared_ptr<BlockNode> b,
                       bool refCapture = false)
        : returnType(std::move(rt)), name(std::move(n)),
          params(std::move(p)), body(std::move(b)), byRef(refCapture) {}

    std::string toString() const override {
        return "NestedFunction(" + name + ")";
    }

    std::string toC() const override {
        // 生成：std::function<Ret(Params)> name = [=](Params) -> Ret { body };
        std::string retC = mapTypeToC(returnType);
        std::string fnType = "std::function<" + retC + "(";
        for (std::size_t i = 0; i < params.size(); ++i){
            if (i > 0) fnType += ", ";
            fnType += mapTypeToC(params[i].type);
        }
        fnType += ")>";

        std::string r = fnType + " " + name + " = ";
        r += std::string("[") + (byRef ? "&" : "=") + "](";
        for (std::size_t i = 0; i < params.size(); ++i){
            if (i > 0) r += ", ";
            if (params[i].isRef) r += mapTypeToC(params[i].type) + "& ";
            else                 r += mapTypeToC(params[i].type) + " ";
            r += params[i].name;
        }
        r += ") -> " + retC + " {\n";
        for (const auto& p : params){
            if (p.constraint.has_value()){
                r += "    " + p.constraint->toCCheck(p.name) + "\n";
            }
        }
        if (body) r += body->toC();
        r += "};\n";
        return r;
    }

    bool needsStringInclude() const override { return true; }
};

class ReturnNode : public AstNode {
public:
    AstNodePtr expression;
    int line = 0;

    explicit ReturnNode(AstNodePtr expr, int srcLine = 0)
        : expression(std::move(expr)), line(srcLine) {}

    std::string toString() const override {
        return "Return(" + (expression ? expression->toString() : "") + ")";
    }
    std::string toC() const override {
        if (!expression) return "return;";
        return "return " + expression->toC() + ";";
    }
    bool needsStringInclude() const override {
        return expression ? expression->needsStringInclude() : false;
    }
};

class IfNode : public AstNode {
public:
    AstNodePtr condition;
    std::shared_ptr<BlockNode> thenBody;
    AstNodePtr elseBranch;   // nullptr | BlockNode | IfNode

    IfNode(AstNodePtr cond, std::shared_ptr<BlockNode> then, AstNodePtr els)
        : condition(std::move(cond)),
          thenBody(std::move(then)),
          elseBranch(std::move(els)) {}

    std::string toString() const override {
        std::string r = "If(" + (condition ? condition->toString() : "") + ") { "
                      + (thenBody ? thenBody->toString() : "") + " }";
        if (elseBranch){
            if (auto nested = std::dynamic_pointer_cast<IfNode>(elseBranch)){
                r += " ElseIf " + nested->toString();
            } else if (auto blk = std::dynamic_pointer_cast<BlockNode>(elseBranch)){
                r += " Else { " + blk->toString() + " }";
            }
        }
        return r;
    }

    std::string toC() const override {
        std::string cond = condition ? condition->toC() : "false";
        std::string r = "if (vox::vox_truthy(" + cond + ")) {\n";
        if (thenBody) r += thenBody->toC();
        r += "}";
        if (elseBranch){
            if (auto nested = std::dynamic_pointer_cast<IfNode>(elseBranch)){
                r += " else " + nested->toC();
            } else if (auto blk = std::dynamic_pointer_cast<BlockNode>(elseBranch)){
                r += " else {\n" + blk->toC() + "}";
            }
        }
        return r;
    }

    bool needsStringInclude() const override {
        if (condition && condition->needsStringInclude())  return true;
        if (thenBody  && thenBody->needsStringInclude())   return true;
        if (elseBranch && elseBranch->needsStringInclude())return true;
        return false;
    }
};

class TernaryNode : public AstNode {
public:
    AstNodePtr condition;
    AstNodePtr thenExpr;
    AstNodePtr elseExpr;

    TernaryNode(AstNodePtr c, AstNodePtr t, AstNodePtr e)
        : condition(std::move(c)),
          thenExpr(std::move(t)),
          elseExpr(std::move(e)) {}

    std::string toString() const override {
        return "Ternary(" + condition->toString() + " ? "
             + thenExpr->toString() + " : " + elseExpr->toString() + ")";
    }

    std::string toC() const override {
        return "(" + condition->toC() + " ? "
             + thenExpr->toC() + " : " + elseExpr->toC() + ")";
    }

    bool needsStringInclude() const override {
        return condition->needsStringInclude()
            || thenExpr->needsStringInclude()
            || elseExpr->needsStringInclude();
    }
};

class WhileNode : public AstNode {
public:
    AstNodePtr condition;
    std::shared_ptr<BlockNode> body;

    WhileNode(AstNodePtr c, std::shared_ptr<BlockNode> b)
        : condition(std::move(c)), body(std::move(b)) {}

    std::string toString() const override {
        return "While(" + (condition ? condition->toString() : "") + ") { "
             + (body ? body->toString() : "") + " }";
    }
    std::string toC() const override {
        std::string r = "while (vox::vox_truthy(" + (condition ? condition->toC() : "false") + ")) {\n";
        if (body) r += body->toC();
        r += "}";
        return r;
    }
    bool needsStringInclude() const override {
        if (condition && condition->needsStringInclude()) return true;
        return body ? body->needsStringInclude() : false;
    }
};

class DoWhileNode : public AstNode {
public:
    std::shared_ptr<BlockNode> body;
    AstNodePtr condition;

    DoWhileNode(std::shared_ptr<BlockNode> b, AstNodePtr c)
        : body(std::move(b)), condition(std::move(c)) {}

    std::string toString() const override {
        return "DoWhile { " + (body ? body->toString() : "")
             + " } While(" + (condition ? condition->toString() : "") + ")";
    }
    std::string toC() const override {
        std::string r = "do {\n";
        if (body) r += body->toC();
        r += "} while (" + (condition ? condition->toC() : "") + ");";
        return r;
    }
    bool needsStringInclude() const override {
        if (condition && condition->needsStringInclude()) return true;
        return body ? body->needsStringInclude() : false;
    }
};

class ForNode : public AstNode {
public:
    AstNodePtr init;
    AstNodePtr cond;
    AstNodePtr step;
    std::shared_ptr<BlockNode> body;
    ForNode(AstNodePtr i, AstNodePtr c, AstNodePtr s, std::shared_ptr<BlockNode> b)
        : init(std::move(i)), cond(std::move(c)), step(std::move(s)), body(std::move(b)) {}

    std::string toString() const override {
        return "For(" + (init ? init->toString() : "") + "; "
                    + (cond ? cond->toString() : "") + "; "
                    + (step ? step->toString() : "") + ") { "
                    + (body ? body->toString() : "") + " }";
    }
    std::string toC() const override {
        auto strip = [](std::string s){
            while (!s.empty() && (s.back() == ';' || s.back() == ' ' || s.back() == '\n'))
                s.pop_back();
            return s;
        };
        std::string r = "for (";
        if (init) r += strip(init->toC());
        r += "; ";
        if (cond) r += strip(cond->toC());
        r += "; ";
        if (step) r += strip(step->toC());
        r += ") {\n";
        if (body) r += body->toC();
        r += "}";
        return r;
    }
};

struct ForeachVar {
    std::string type;
    std::string name;
    AstNodePtr iterable;
};

class ForeachNode : public AstNode {
public:
    std::vector<ForeachVar> vars;
    std::shared_ptr<BlockNode> body;

    ForeachNode(std::vector<ForeachVar> v, std::shared_ptr<BlockNode> b)
        : vars(std::move(v)), body(std::move(b)) {}

    std::string toString() const override {
        std::string r = "Foreach(";
        for (std::size_t i = 0; i < vars.size(); ++i){
            if (i > 0) r += ", ";
            r += vars[i].type + " " + vars[i].name + " in " + vars[i].iterable->toString();
        }
        r += ") { " + (body ? body->toString() : std::string()) + " }";
        return r;
    }

    std::string toC() const override {
        std::size_t n = vars.size();

        std::string r = "{\n";

        // 1) 绑定可迭代对象 + 创建迭代器
        for (std::size_t i = 0; i < n; ++i) {
            std::string idx = std::to_string(i);
            r += "    auto&& __vox_coll_" + idx + " = " + vars[i].iterable->toC() + ";\n";
            r += "    auto __vox_it_" + idx + " = __vox_coll_" + idx + "._begin_();\n";
        }

        // 2) 循环头：所有迭代器都结束时停止
        //    用 || 而不是 && —— 循环到最长
        r += "    for (; ";
        for (std::size_t i = 0; i < n; ++i) {
            if (i > 0) r += " || ";
            r += "!__vox_it_" + std::to_string(i) + "._done_()";
        }
        r += "; ) {\n";

        // 3) 绑定变量
        if (n == 1) {
            // 单变量：纯 T —— 向后兼容，零成本
            r += "        " + mapTypeToC(vars[0].type) + " " + vars[0].name
               + " = __vox_it_0._current_();\n";
            r += "        __vox_it_0._next_();\n";
        } else {
            // 多变量：std::optional<T> —— null 表示该集合已耗尽
            for (std::size_t i = 0; i < n; ++i) {
                std::string idx = std::to_string(i);
                std::string cType = mapTypeToC(vars[i].type);
                r += "        std::optional<" + cType + "> " + vars[i].name + ";\n";
                r += "        if (!__vox_it_" + idx + "._done_()) {\n";
                r += "            " + vars[i].name + " = __vox_it_" + idx + "._current_();\n";
                r += "            __vox_it_" + idx + "._next_();\n";
                r += "        }\n";
            }
        }

        // 4) 循环体
        r += body->toC();

        // 5) 结束
        r += "    }\n";
        r += "}";
        return r;
    }

    bool needsStringInclude() const override {
        for (const auto& v : vars){
            if (v.iterable && v.iterable->needsStringInclude()) return true;
        }
        return body ? body->needsStringInclude() : false;
    }
};

class IsNullNode : public AstNode {
public:
    AstNodePtr expr;

    explicit IsNullNode(AstNodePtr e) : expr(std::move(e)) {}

    std::string toString() const override {
        return "IsNull(" + expr->toString() + ")";
    }

    std::string toC() const override {
        return "vox::vox_is_null(" + expr->toC() + ")";
    }

    bool needsStringInclude() const override { return true; }
};

class RaiseNode : public AstNode {
public:
    std::string exceptionName;
    AstNodePtr message;
    std::string path;
    std::string code;
    int line;
    int col1Start, col1Len, col2Start, col2Len;
    std::vector<std::string> locals;

    RaiseNode(std::string argName, AstNodePtr argMessage,
              std::string argPath = "", std::string argCode = "",
              int argLine = 0,
              int c1 = 0, int l1 = 0, int c2 = -1, int l2 = 0,
              std::vector<std::string> loc = {})
        : exceptionName(std::move(argName)),
          message(std::move(argMessage)),
          path(std::move(argPath)),
          code(std::move(argCode)),
          line(argLine),
          col1Start(c1), col1Len(l1),
          col2Start(c2), col2Len(l2),
          locals(std::move(loc)) {}

    std::string toString() const override {
        return "Raise(" + exceptionName + "("
             + (message ? message->toString() : std::string()) + "))";
    }

    std::string toC() const override {
        std::string msgC = message ? message->toC() : "\"\"";
        std::string msgArg;
        if (std::dynamic_pointer_cast<StringNode>(message)) {
            msgArg = msgC;
        } else {
            msgArg = "((" + msgC + ").raw())";
        }

        // ★ 生成 locals map
        std::string localsArg = "std::map<std::string, std::string>{";
        for (std::size_t i = 0; i < locals.size(); ++i){
            if (i > 0) localsArg += ", ";
            localsArg += "{\"" + escapeCString(locals[i]) + "\", "
                       + "(vox::vox_str(" + locals[i] + ")).raw()}";
        }
        localsArg += "}";

        return "throw vox::" + exceptionName + "(\""
            + escapeCString(exceptionName) + "\", "
            + msgArg + ", "
            + "\"" + escapeCString(path)    + "\", "
            + "\"" + escapeCString(code)    + "\", "
            + std::to_string(line) + ", "
            + std::to_string(col1Start) + ", "
            + std::to_string(col1Len)   + ", "
            + std::to_string(col2Start) + ", "
            + std::to_string(col2Len)   + ", "
            + localsArg + ", "
            + "__func__"
            + ");";
    }

    bool needsStringInclude() const override {
        return message ? message->needsStringInclude() : false;
    }
};

class FunctionDeclNode : public AstNode {
public:
    std::vector<Attribute> attributes;
    std::string returnType;
    std::string name;
    std::vector<Param> params;
    std::shared_ptr<BlockNode> body;
    bool isExtern;
    int line = 0;
    std::vector<std::string> templateParams;   // ★ 模板参数名列表（空表示非模板）

    FunctionDeclNode(std::string retType, std::string funcName,
                     std::vector<Param> argParams,
                     std::shared_ptr<BlockNode> argBody,
                     bool argIsExtern = false,
                     int srcLine = 0)
        : returnType(std::move(retType)), name(std::move(funcName)),
          params(std::move(argParams)), body(std::move(argBody)),
          isExtern(argIsExtern), line(srcLine) {}

    std::string toString() const override {
        std::string result = "Function(ReturnType(" + returnType + "), Name(" + name + "), Params(";
        for (std::size_t i = 0; i < params.size(); ++i){
            if (i > 0) result += ",";
            result += params[i].type + " " + params[i].name;
        }
        result += "), Body(" + (body ? body->toString() : std::string("extern")) + "))";
        return result;
    }

    std::string toC() const override {
        if (isExtern){
            // ...（保持原样）...
            std::string result = mapTypeToC(returnType) + " " + name + "(";
            for (std::size_t i = 0; i < params.size(); ++i){
                if (i > 0) result += ", ";
                if (params[i].isRef) result += mapTypeToC(params[i].type) + "& ";
                else                 result += mapTypeToC(params[i].type) + " ";
                result += params[i].name;
            }
            result += ");";
            return result;
        }

        bool hasVariadic = false;
        for (const auto& p : params){
            if (p.isVariadic){ hasVariadic = true; break; }
        }

        // ★ 组装 template 头
        std::string templateHeader;
        if (!templateParams.empty() || hasVariadic){
            templateHeader = "template <";
            bool first = true;
            for (const auto& p : templateParams){
                if (!first) templateHeader += ", ";
                first = false;
                templateHeader += "typename " + p;
            }
            if (hasVariadic){
                if (!first) templateHeader += ", ";
                templateHeader += "typename... __VoxVariadic";
            }
            templateHeader += ">\n";
        }

        std::string result = templateHeader;
        if (hasVariadic){
            result += mapTypeToC(returnType) + " " + name + "(";

            bool first = true;
            for (const auto& p : params){
                if (p.isVariadic) continue;
                if (!first) result += ", ";
                first = false;
                if (p.isRef) result += mapTypeToC(p.type) + "& ";
                else result += mapTypeToC(p.type) + " ";
                result += p.name;
                if (p.defaultValue){
                    result += " = " + p.defaultValue->toC();
                }
            }
            for (const auto& p : params){
                if (!p.isVariadic) continue;
                if (!first) result += ", ";
                first = false;
                result += "__VoxVariadic... __vox_var_" + p.name;
            }
            result += ") {\n";
            for (const auto& p : params){
                if (p.isVariadic){
                    result += "    std::vector<" + mapTypeToC(p.type) + "> " + p.name
                            + " = {vox::vox_str(__vox_var_" + p.name + ")...};\n";
                }
            }
            for (const auto& p : params){
                if (!p.isVariadic && p.constraint.has_value()){
                    result += "    " + p.constraint->toCCheck(p.name) + "\n";
                }
            }
            result += body->toC();
            result += "}";
        } else {
            result += mapTypeToC(returnType) + " " + name + "(";
            for (std::size_t i = 0; i < params.size(); ++i){
                if (i > 0) result += ", ";
                result += mapTypeToC(params[i].type) + " " + params[i].name;
                if (params[i].defaultValue){
                    result += " = " + params[i].defaultValue->toC();
                }
            }
            result += ") {\n";
            for (const auto& p : params){
                if (p.constraint.has_value()){
                    result += "    " + p.constraint->toCCheck(p.name) + "\n";
                }
            }
            result += body->toC();
            result += "}";
        }
        return result;
    }

    bool needsStringInclude() const override {
        if (returnType == "string") return true;
        for (const auto& p : params){
            if (p.type == "string") return true;
        }
        return body ? body->needsStringInclude() : false;
    }
    bool isTopLevelDefinition() const override { return true; }
};

class ExternBlockNode : public AstNode {
public:
    std::vector<AstNodePtr> decls;
    void add(AstNodePtr n){ decls.push_back(std::move(n)); }
    std::string toString() const override {
        return "Extern(" + std::to_string(decls.size()) + " decls)";
    }
    std::string toC() const override {
        std::string r;
        for (const auto& d : decls){
            r += d->toC();
            r += "\n";
        }
        return r;
    }
    bool isTopLevelDefinition() const override { return false; }  // ★ ProgramNode 已单独输出
};

class NamespaceNode : public AstNode {
public:
    std::string name;
    bool importAll;
    std::vector<AstNodePtr> statements;

    NamespaceNode(std::string n, bool all)
        : name(std::move(n)), importAll(all) {}

    void add(AstNodePtr node) { statements.push_back(std::move(node)); }

    std::string toString() const override {
        return "Namespace(" + name + (importAll ? ".*" : "") + ")";
    }

    // 递归收集所有 header include
    void collectIncludes(std::string& out) const {
        for (const auto& s : statements){
            if (s->isHeaderInclude()){
                out += s->headerIncludeLine() + "\n";
            } else if (auto ns = std::dynamic_pointer_cast<NamespaceNode>(s)){
                ns->collectIncludes(out);   // <-- 递归
            }
        }
    }

    std::string toC(bool skipIncludes) const override {
        std::string r;

        if (!skipIncludes){
            std::string includes;
            collectIncludes(includes);
            if (!includes.empty()){
                r += includes + "\n";
            }
        }

        r += "namespace " + name + " {\n";

        // ★ 先输出所有 class 的前向声明（含 template 头）
        for (const auto& s : statements){
            std::string decl = s->forwardDecl();
            if (!decl.empty()){
                r += decl + "\n";
            }
        }
        r += "\n";

        for (const auto& s : statements){
            if (s->isHeaderInclude()) continue;
            if (auto ns = std::dynamic_pointer_cast<NamespaceNode>(s)){
                r += ns->toC(true) + "\n";   // <-- 嵌套时跳过 include
            } else {
                r += s->toC() + "\n";
            }
        }
        r += "}\n";

        if (importAll){
            r += "using namespace " + name + ";\n";
        }
        return r;
    }

    std::string toC() const override {
        return toC(false);
    }

    bool needsStringInclude() const override {
        for (const auto& s : statements){
            if (s->needsStringInclude()) return true;
        }
        return false;
    }

    bool isTopLevelDefinition() const override { return true; }
};

// ===================== try / catch / finally / break / continue / switch =====================
struct CatchClause {
    std::string exceptionType;              // 异常类型名（如 "ValueException"）
    std::string variableName;               // 绑定变量名（可为空）
    std::shared_ptr<BlockNode> body;        // catch 体
};

class TryNode : public AstNode {
public:
    std::shared_ptr<BlockNode> tryBody;
    std::vector<CatchClause> catches;
    std::shared_ptr<BlockNode> finallyBody;

    TryNode(std::shared_ptr<BlockNode> tb,
            std::vector<CatchClause> cs,
            std::shared_ptr<BlockNode> fb)
        : tryBody(std::move(tb)),
          catches(std::move(cs)),
          finallyBody(std::move(fb)) {}

    std::string toString() const override {
        std::string r = "Try { " + (tryBody ? tryBody->toString() : "") + " }";
        for (const auto& c : catches){
            r += " Catch(" + c.exceptionType;
            if (!c.variableName.empty()) r += " " + c.variableName;
            r += ") { " + (c.body ? c.body->toString() : "") + " }";
        }
        if (finallyBody){
            r += " Finally { " + finallyBody->toString() + " }";
        }
        return r;
    }

    std::string toC() const override {
        std::string tryPart = "try {\n";
        if (tryBody) tryPart += tryBody->toC();
        tryPart += "}";
        for (const auto& c : catches){
            // 关键：加 vox:: 前缀
            tryPart += " catch (const vox::" + c.exceptionType + "& " + c.variableName + ") {\n";
            tryPart += c.body->toC();
            tryPart += "}";
        }

        if (!finallyBody){
            return tryPart;
        }

        // finally：正常路径执行一次，异常路径执行一次并重新抛出
        std::string r;
        r += "try {\n";
        r += "    " + tryPart + "\n";
        r += "} catch (...) {\n";
        r += finallyBody->toC();
        r += "    throw;\n";
        r += "}\n";
        r += finallyBody->toC();
        return r;
    }

    bool needsStringInclude() const override {
        if (tryBody && tryBody->needsStringInclude()) return true;
        for (const auto& c : catches){
            if (c.body && c.body->needsStringInclude()) return true;
        }
        return finallyBody ? finallyBody->needsStringInclude() : false;
    }
};

class BreakNode : public AstNode {
public:
    int line = 0;
    explicit BreakNode(int srcLine = 0) : line(srcLine) {}
    std::string toString() const override { return "Break()"; }
    std::string toC() const override { return "break;"; }
};

class ContinueNode : public AstNode {
public:
    int line = 0;
    explicit ContinueNode(int srcLine = 0) : line(srcLine) {}
    std::string toString() const override { return "Continue()"; }
    std::string toC() const override { return "continue;"; }
};

struct SwitchCase {
    AstNodePtr value;                       // case 的值（nullptr = default）
    std::shared_ptr<BlockNode> body;
};

class SwitchNode : public AstNode {
public:
    AstNodePtr expr;
    std::vector<SwitchCase> cases;

    SwitchNode(AstNodePtr e, std::vector<SwitchCase> c)
        : expr(std::move(e)), cases(std::move(c)) {}

    std::string toString() const override {
        std::string r = "Switch(" + (expr ? expr->toString() : "") + ") {";
        for (const auto& c : cases){
            r += c.value ? " Case(" + c.value->toString() + ")" : " Default";
            r += " { " + (c.body ? c.body->toString() : "") + " }";
        }
        r += " }";
        return r;
    }

    std::string toC() const override {
        std::string r = "switch (" + (expr ? expr->toC() : "") + ") {\n";
        for (const auto& c : cases){
            if (c.value){
                r += "case " + c.value->toC() + ":\n";
            } else {
                r += "default:\n";
            }
            if (c.body) r += c.body->toC();
        }
        r += "}";
        return r;
    }

    bool needsStringInclude() const override {
        if (expr && expr->needsStringInclude()) return true;
        for (const auto& c : cases){
            if (c.value && c.value->needsStringInclude()) return true;
            if (c.body && c.body->needsStringInclude()) return true;
        }
        return false;
    }
};

// ===================== 类系统 =====================
struct ClassField {
    std::string access;
    bool isStatic;
    std::string typeName;
    std::string name;
    AstNodePtr init;
};

struct ClassMethod {
    std::string access;
    bool isStatic;
    bool isOverride;
    bool isCtor;
    std::string returnType;
    std::string name;
    std::vector<Param> params;
    std::shared_ptr<BlockNode> body;
    int line = 0;
};

class ClassNode : public AstNode {
public:
    std::vector<Attribute> attributes;
    std::string name;
    std::vector<std::string> bases;
    std::vector<bool> baseIsInterface;      // ← 和 bases 一一对应
    std::vector<ClassField> fields;
    std::vector<ClassMethod> methods;
    bool isStruct = false;
    bool isInterface = false;
    std::vector<std::string> templateParams;   // ★

    ClassNode() = default;
    ClassNode(std::string n, std::vector<std::string> b)
        : name(std::move(n)), bases(std::move(b)) {}

    std::string toString() const override {
        std::string r = "Class(" + name;
        if (!bases.empty()){
            r += " extends ";
            for (std::size_t i = 0; i < bases.size(); ++i){
                if (i > 0) r += ",";
                r += bases[i];
            }
        }
        r += ")";
        return r;
    }

    std::string toC() const override {
        // ========== interface：纯虚类 ==========
        if (isInterface) {
            std::string r;
            // ★ 模板头
            if (!templateParams.empty()){
                r += "template <";
                for (std::size_t i = 0; i < templateParams.size(); ++i){
                    if (i > 0) r += ", ";
                    r += "typename " + templateParams[i];
                }
                r += ">\n";
            }
            r += "class " + name;
            if (!bases.empty()){
                r += " : ";
                for (std::size_t i = 0; i < bases.size(); ++i){
                    if (i > 0) r += ", ";
                    bool isIface = (i < baseIsInterface.size()) ? baseIsInterface[i] : true;
                    if (isIface){
                        r += "virtual public " + bases[i];   // ★ 虚继承
                    } else {
                        r += "public " + bases[i];
                    }
                }
            }
            r += " {\n";
            r += "public:\n";
            r += "    virtual ~" + name + "() = default;\n\n";

            // 纯虚方法
            for (const auto& m : methods){
                r += "    virtual " + mapTypeToC(m.returnType) + " " + m.name + "(";
                for (std::size_t i = 0; i < m.params.size(); ++i){
                    if (i > 0) r += ", ";
                    r += mapTypeToC(m.params[i].type) + " " + m.params[i].name;
                    if (m.params[i].defaultValue){
                        r += " = " + m.params[i].defaultValue->toC();
                    }
                }
                r += ") = 0;\n";
            }

            // ★ 类型名
            r += "\n    virtual vox::String vox_type_name() const { return \"" + name + "\"; }\n";

            // ★ 类型检查（链式）
            r += "    virtual bool vox_is_type(const vox::String& t) const {\n";
            r += "        if (t == \"" + name + "\") return true;\n";
            for (std::size_t i = 0; i < bases.size(); ++i){
                bool isIface = (i < baseIsInterface.size()) ? baseIsInterface[i] : true;
                if (isIface){
                    r += "        if (" + bases[i] + "::vox_is_type(t)) return true;\n";
                }
            }
            r += "        return false;\n";
            r += "    }\n";

            r += "};\n";
            return r;
        }
        std::string r;
        // ★ 模板头
        if (!templateParams.empty()){
            r += "template <";
            for (std::size_t i = 0; i < templateParams.size(); ++i){
                if (i > 0) r += ", ";
                r += "typename " + templateParams[i];
            }
            r += ">\n";
        }
        r += "class " + name;
        if (!bases.empty()){
            r += " : ";
            for (std::size_t i = 0; i < bases.size(); ++i){
                if (i > 0) r += ", ";
                bool isIface = (i < baseIsInterface.size()) ? baseIsInterface[i] : false;
                if (isIface){
                    r += "virtual public " + bases[i];   // ★ interface 用虚继承
                } else {
                    r += "public " + bases[i];
                }
            }
        }
        r += " {\n";

        // ========== public 区域 ==========
        r += "public:\n";

        // ★ 1. 默认构造（保留原有 _init_ 生成的构造）
        //    （_init_ 会被 ClassMethod 机制生成为同名构造函数，这里不动）

        // ★ 2. 显式拷贝构造（成员初始化列表形式）
        r += "    " + name + "(const " + name + "& other)";

        // 构造初始化列表（非静态字段 + 基类）
        std::vector<std::string> initList;
        for (const auto& f : fields){
            if (f.isStatic) continue;    // ← 静态字段不能出现在初始化列表
            initList.push_back(f.name + "(other." + f.name + ")");
        }
        for (std::size_t i = 0; i < bases.size(); ++i){
            bool isIface = (i < baseIsInterface.size()) ? baseIsInterface[i] : false;
            if (isIface){
                initList.push_back(bases[i] + "(other)");   // 虚基类也照写，C++ 会正确处理
            } else {
                initList.push_back(bases[i] + "(other)");
            }
        }

        if (!initList.empty()){
            r += "\n        : ";
            for (std::size_t i = 0; i < initList.size(); ++i){
                if (i > 0) r += ",\n          ";
                r += initList[i];
            }
            r += "\n    ";
        }
        r += "{}\n\n";

        // ★ 3. 显式拷贝赋值
        r += "    " + name + "& operator=(const " + name + "& other) {\n";
        r += "        if (this != &other) {\n";
        for (const auto& f : fields){
            if (f.isStatic) continue;    // ← 静态字段不属于对象
            r += "            this->" + f.name + " = other." + f.name + ";\n";
        }
        for (std::size_t i = 0; i < bases.size(); ++i){
            bool isIface = (i < baseIsInterface.size()) ? baseIsInterface[i] : false;
            if (!isIface){
                // interface 无字段，可跳过赋值
                r += "            " + bases[i] + "::operator=(other);\n";
            }
        }
        r += "        }\n";
        r += "        return *this;\n";
        r += "    }\n\n";

        // ★ 默认构造（用户没定义无参 _init_ 时才生成）
        bool hasDefaultCtor = false;
        for (const auto& m : methods){
            if (m.isCtor && m.params.empty()){
                hasDefaultCtor = true;
                break;
            }
        }
        if (!hasDefaultCtor){
            r += "    " + name + "() = default;\n\n";
        }

        // ★ 无副作用构造（专用于全局变量声明，避免触发用户 _init_）
        r += "    " + name + "(vox::NoInit) {}\n\n";

        // ========== struct：自动生成构造 / 运算符 / toString ==========
        if (isStruct){
            // 是否已有用户自定义构造函数
            bool hasCtor = false;
            for (const auto& m : methods){
                if (m.isCtor){ hasCtor = true; break; }
            }

            // ★ 自动构造（用户没写 _init_ 才生成）
            if (!hasCtor){
                // 字段构造（默认构造由外层统一生成）
                bool anyField = false;
                for (const auto& f : fields){
                    if (!f.isStatic){ anyField = true; break; }
                }
                if (anyField){
                    r += "    " + name + "(";
                    bool first = true;
                    for (const auto& f : fields){
                        if (f.isStatic) continue;
                        if (!first) r += ", ";
                        first = false;
                        r += mapTypeToC(f.typeName) + " _" + f.name;
                    }
                    r += ")";
                    bool firstInit = true;
                    for (const auto& f : fields){
                        if (f.isStatic) continue;
                        if (firstInit){ r += "\n        : "; firstInit = false; }
                        else            r += ",\n          ";
                        r += f.name + "(_" + f.name + ")";
                    }
                    r += "\n    {}\n\n";
                }
            }

            // ★ toString
            r += "    vox::String toString() const {\n";
            r += "        vox::String _r = \"" + name + "(\";\n";
            {
                bool first = true;
                for (const auto& f : fields){
                    if (f.isStatic) continue;
                    if (!first){
                        r += "        _r += \", \";\n";
                    }
                    first = false;
                    r += "        _r += \"" + f.name + "=\";\n";
                    r += "        _r += vox::vox_str(" + f.name + ");\n";
                }
            }
            r += "        _r += \")\";\n";
            r += "        return _r;\n";
            r += "    }\n\n";
        }

        // ★ 类型名（typeof 用）
        r += "    virtual vox::String vox_type_name() const { return \"" + name + "\"; }\n\n";

        // ★ 类型检查（is 用，支持继承）
        if (bases.empty()){
            r += "    bool vox_is_type(const vox::String& t) const {\n";
            r += "        return t == \"" + name + "\";\n";
            r += "    }\n\n";
        } else {
            r += "    bool vox_is_type(const vox::String& t) const {\n";
            r += "        if (t == \"" + name + "\") return true;\n";
            for (const auto& b : bases){
                r += "        if (" + b + "::vox_is_type(t)) return true;\n";
            }
            r += "        return false;\n";
            r += "    }\n\n";
        }

        // ========== 原有的 fields / methods 输出 ==========

        auto emitFields = [&](const std::string& access){
            for (const auto& f : fields){
                if (f.access != access) continue;
                if (f.isStatic) r += "    static ";
                r += "    " + mapTypeToC(f.typeName) + " " + f.name;
                if (f.init) r += " = " + f.init->toC();
                r += ";\n";
            }
        };
        auto emitMethods = [&](const std::string& access){
            for (const auto& m : methods){
                if (m.access != access) continue;

                bool hasVariadic = false;
                for (const auto& p : m.params){
                    if (p.isVariadic){ hasVariadic = true; break; }
                }

                if (hasVariadic && !m.isCtor){
                    r += "    template<typename... __VoxVariadic>\n";
                    r += "    ";
                    if (m.isStatic) r += "static ";
                    std::string rt = m.returnType.empty() ? "void" : mapTypeToC(m.returnType);
                    r += rt + " " + m.name + "(";

                    bool first = true;
                    // 先输出非可变参数
                    for (const auto& p : m.params){
                        if (p.isVariadic) continue;
                        if (!first) r += ", ";
                        first = false;
                        if (p.isRef) r += mapTypeToC(p.type) + "& ";
                        else r += mapTypeToC(p.type) + " ";
                        r += p.name;
                        if (p.defaultValue){
                            r += " = " + p.defaultValue->toC();
                        }
                    }
                    // 再输出可变参数包
                    for (const auto& p : m.params){
                        if (!p.isVariadic) continue;
                        if (!first) r += ", ";
                        first = false;
                        r += "__VoxVariadic... __vox_var_" + p.name;
                    }
                    r += ") {\n";
                    for (const auto& p : m.params){
                        if (p.isVariadic){
                            r += "        std::vector<" + mapTypeToC(p.type) + "> " + p.name
                                + " = {vox::vox_str(__vox_var_" + p.name + ")...};\n";
                        }
                    }
                    for (const auto& p : m.params){
                        if (!p.isVariadic && p.constraint.has_value()){
                            r += "        " + p.constraint->toCCheck(p.name) + "\n";
                        }
                    }
                    r += m.body->toC();
                    r += "    }\n";
                } else {
                    r += "    ";
                    if (m.isStatic) r += "static ";
                    if (m.isOverride && !m.isCtor) r += "virtual ";
                    if (m.isCtor){
                        r += name + "(";
                    } else {
                        std::string rt = m.returnType.empty() ? "void" : mapTypeToC(m.returnType);
                        r += rt + " " + m.name + "(";
                    }
                    for (std::size_t i = 0; i < m.params.size(); ++i){
                        if (i > 0) r += ", ";
                        if (m.params[i].isRef) r += mapTypeToC(m.params[i].type) + "& ";
                        else r += mapTypeToC(m.params[i].type) + " ";
                        r += m.params[i].name;
                        if (m.params[i].defaultValue){
                            if (std::dynamic_pointer_cast<NullNode>(m.params[i].defaultValue)){
                                r +=     " = " + nullAsType(m.params[i].type);
                            } else {
                                r += " = " + m.params[i].defaultValue->toC();
                            }
                        }
                    }
                    r += ")";
                    if (m.isOverride && !m.isCtor && !bases.empty()) r += " override";
                    r += " {\n";
                    for (const auto& p : m.params){
                        if (p.constraint.has_value()){
                            r += "        " + p.constraint->toCCheck(p.name) + "\n";
                        }
                    }
                    r += m.body->toC();
                    r += "    }\n";
                }
            }
        };  

        r += "public:\n";
        emitFields("public"); emitMethods("public");
        r += "protected:\n";
        emitFields("protected"); emitMethods("protected");
        r += "private:\n";
        emitFields("private"); emitMethods("private");

        // ★ 有 _bool_ 魔法方法时，生成 operator bool（供 if / && / check 等使用）
        for (const auto& m : methods){
            if (m.name == "_bool_" && !m.isStatic && !m.isCtor && m.params.empty()){
                r += "public:\n";
                r += "    operator bool() { return _bool_(); }\n\n";
                break;
            }
        }

        r += "};";

        // ========== 自由函数：运算符重载（必须在 class 外部！） ==========
        std::set<std::string> magic;
        for (const auto& m : methods){
            if (m.isStatic || m.isCtor) continue;
            if (m.params.size() != 1) continue;
            magic.insert(m.name);
        }

        auto emitBinOp = [&](const std::string& magicName, const std::string& op){
            if (magic.count(magicName) == 0) return;
            for (const auto& m : methods){
                if (m.name != magicName) continue;
                if (m.isStatic || m.isCtor) continue;
                if (m.params.size() != 1) continue;

                std::string retC = mapTypeToC(m.returnType);
                std::string paramC = mapTypeToC(m.params[0].type);

                r += "inline " + retC + " operator" + op
                   + "(const " + name + "& _a, const " + paramC + "& _b) {\n";
                r += "    return const_cast<" + name + "&>(_a)." + magicName + "(_b);\n";
                r += "}\n\n";
            }
        };
        emitBinOp("_add_", "+");
        emitBinOp("_sub_", "-");
        emitBinOp("_mul_", "*");
        emitBinOp("_div_", "/");
        emitBinOp("_mod_", "%");
        emitBinOp("_eq_",  "==");
        emitBinOp("_ne_",  "!=");
        emitBinOp("_lt_",  "<");
        emitBinOp("_le_",  "<=");
        emitBinOp("_gt_",  ">");
        emitBinOp("_ge_",  ">=");

        // struct 自动 == （用户没定义 _eq_ 时）
        if (isStruct && magic.count("_eq_") == 0){
            r += "inline bool operator==(const " + name + "& _a, const " + name + "& _b) {\n";
            std::string cmp;
            for (const auto& f : fields){
                if (f.isStatic) continue;
                if (!cmp.empty()) cmp += " && ";
                cmp += "_a." + f.name + " == _b." + f.name;
            }
            if (cmp.empty()) r += "    return true;\n";
            else             r += "    return " + cmp + ";\n";
            r += "}\n\n";
        }

        // 自动 != （用户没定义 _ne_，但 == 可用时）
        if (magic.count("_ne_") == 0){
            if (isStruct || magic.count("_eq_")){
                r += "inline bool operator!=(const " + name + "& _a, const " + name + "& _b) {\n";
                r += "    return !(_a == _b);\n";
                r += "}\n\n";
            }
        }

        // 自动 > <= >= （用户定义了 _lt_ 时）
        if (magic.count("_lt_")){
            if (magic.count("_gt_") == 0){
                r += "inline bool operator>(const " + name + "& _a, const " + name + "& _b) {\n";
                r += "    return _b < _a;\n";
                r += "}\n\n";
            }
            if (magic.count("_le_") == 0){
                r += "inline bool operator<=(const " + name + "& _a, const " + name + "& _b) {\n";
                r += "    return !(_b < _a);\n";
                r += "}\n\n";
            }
            if (magic.count("_ge_") == 0){
                r += "inline bool operator>=(const " + name + "& _a, const " + name + "& _b) {\n";
                r += "    return !(_a < _b);\n";
                r += "}\n\n";
            }
        }
        return r;
    }

    bool needsStringInclude() const override {
        for (const auto& f : fields){
            if (f.typeName == "string") return true;
            if (f.init && f.init->needsStringInclude()) return true;
        }
        for (const auto& m : methods){
            if (m.returnType == "string") return true;
            for (const auto& p : m.params){
                if (p.type == "string") return true;
            }
            if (m.body && m.body->needsStringInclude()) return true;
        }
        return false;
    }

    std::string forwardDecl() const override {
        std::string r;
        if (!templateParams.empty()){
            r += "template <";
            for (std::size_t i = 0; i < templateParams.size(); ++i){
                if (i > 0) r += ", ";
                r += "typename " + templateParams[i];
            }
            r += ">\n";
        }
        r += "class " + name + ";";
        return r;
    }

    bool isTopLevelDefinition() const override { return true; }
};

class SemicolonNode : public AstNode {
public:
    std::string toString() const override { return "Semicolon()"; }
    std::string toC() const override { return ""; }
};

class EndNode : public AstNode {
public:
    std::string toString() const override { return "END()"; }
    std::string toC() const override { return ""; }
};

// ===================== ProgramNode =====================
class ProgramNode : public AstNode {
public:
    std::vector<AstNodePtr> statements;
    ProgramNode() = default;
    explicit ProgramNode(std::vector<AstNodePtr> argStatements)
        : statements(std::move(argStatements)) {}
    void add(AstNodePtr node){ statements.push_back(std::move(node)); }

    std::string toString() const override {
        std::string result;
        for (std::size_t i = 0; i < statements.size(); ++i){
            result += statements[i]->toString();
            if (i + 1 < statements.size()) result += "\n";
        }
        return result;
    }

    std::string toC() const override {
        std::string result;
        result += "#include <iostream>\n";
        result += "#include <string>\n";
        result += "#include <vector>\n";
        result += "#include <map>\n";
        result += "#include <initializer_list>\n";
        result += "#include <utility>\n";
        result += "#include <cstdint>\n\n";
        result += "#include <functional>\n\n";
        result += "#include \"vox_runtime.h\"\n\n";

        // ---- 顶层 ImportNode（#include "xxx.h"） ----
        std::string topIncludes;
        for (const auto& stmt : statements){
            if (stmt->isHeaderInclude()){
                topIncludes += stmt->headerIncludeLine() + "\n";
            }
        }
        if (!topIncludes.empty()){
            result += topIncludes + "\n";
        }

        // ---- 用户自定义异常 ----
        std::vector<std::string> userExceptions;
        for (const auto& s : statements){
            if (auto c = std::dynamic_pointer_cast<ClassNode>(s)){
                for (const auto& b : c->bases){
                    if (b == "Exception"){
                        userExceptions.push_back(c->name);
                        break;
                    }
                }
            }
        }
        if (!userExceptions.empty()){
            result += "namespace vox {\n";
            result += "#define VOX_DEFINE_EXCEPTION(name) \\\n";
            result += "    class name : public Exception { \\\n";
            result += "    public: \\\n";
            result += "        name() : Exception() {} \\\n";
            result += "        name(std::string t, std::string m) : Exception(std::move(t), std::move(m)) {} \\\n";
            result += "        name(std::string t, std::string m, std::string p, std::string c, int l) \\\n";
            result += "            : Exception(std::move(t), std::move(m), std::move(p), std::move(c), l) {} \\\n";
            result += "        name(std::string t, std::string m, std::string p, std::string sl, \\\n";
            result += "             int l, int c1, int l1, int c2, int l2) \\\n";
            result += "            : Exception(std::move(t), std::move(m), std::move(p), std::move(sl), \\\n";
            result += "                        l, c1, l1, c2, l2) {} \\\n";
            result += "    };\n\n";
        }

        // ---- 先输出顶层 ExternBlockNode ----
        for (const auto& stmt : statements){
            if (std::dynamic_pointer_cast<ExternBlockNode>(stmt)){
                result += stmt->toC() + "\n";
            }
        }

        // ---- 输出 namespace / class / enum（跳过全局变量和函数） ----
        for (const auto& stmt : statements){
            if (stmt->isTopLevelDefinition()){
                if (auto var = std::dynamic_pointer_cast<VariableDeclNode>(stmt)){
                    if (var->isGlobal) continue;
                }
                if (std::dynamic_pointer_cast<FunctionDeclNode>(stmt)) continue;
                result += stmt->toC();
                result += "\n\n";
            }
        }

        // ---- ★ 收集所有顶层枚举名（用于全局变量声明判断） ----
        std::set<std::string> enumNames;
        for (const auto& stmt : statements){
            if (auto en = std::dynamic_pointer_cast<EnumNode>(stmt)){
                enumNames.insert(en->name);
            }
        }

        // ---- 输出全局变量的"声明"（不初始化） ----
        auto isBuiltinCType = [&](const std::string& t){
            return t == "int" || t == "int8_t" || t == "int16_t" || t == "int32_t" || t == "int64_t"
                || t == "uint8_t" || t == "uint16_t" || t == "uint32_t" || t == "uint64_t"
                || t == "bool" || t == "char" || t == "float" || t == "double"
                || t == "void"
                || t.compare(0, 5, "vox::") == 0
                || t.compare(0, 14, "std::function<") == 0   // ★ 函数类型
                || enumNames.count(t) > 0;
        };

        for (const auto& stmt : statements){
            if (auto var = std::dynamic_pointer_cast<VariableDeclNode>(stmt)){
                if (var->isGlobal){
                    std::string baseType = mapTypeToC(var->typeName);
                    if (var->isConst){
                        for (std::size_t i = 0; i < var->names.size(); ++i){
                            result += "static const " + baseType + " " + var->names[i];
                            if (i < var->values.size() && var->values[i]){
                                result += " = " + var->values[i]->toC();
                            }
                            result += ";\n";
                        }
                    } else {
                        for (std::size_t i = 0; i < var->names.size(); ++i){
                            if (isBuiltinCType(baseType)){
                                result += "static " + baseType + " " + var->names[i] + "{};\n";
                            } else {
                                result += "static " + baseType + " " + var->names[i] + "{vox::NoInit()};\n";
                            }
                        }
                    }
                }
            }
        }
        result += "\n";

        // ---- ★ 输出所有顶层函数的前向声明（解决前向引用）----
        for (const auto& stmt : statements){
            if (auto fn = std::dynamic_pointer_cast<FunctionDeclNode>(stmt)){
                if (fn->isExtern) continue;

                bool hasVariadic = false;
                for (const auto& p : fn->params){
                    if (p.isVariadic){ hasVariadic = true; break; }
                }

                // ★ 合并模板参数 + variadic
                if (!fn->templateParams.empty() || hasVariadic){
                    result += "template <";
                    bool first = true;
                    for (const auto& p : fn->templateParams){
                        if (!first) result += ", ";
                        first = false;
                        result += "typename " + p;
                    }
                    if (hasVariadic){
                        if (!first) result += ", ";
                        result += "typename... __VoxVariadic";
                    }
                    result += ">\n";
                }
                result += mapTypeToC(fn->returnType) + " " + fn->name + "(";
                for (std::size_t i = 0; i < fn->params.size(); ++i){
                    if (i > 0) result += ", ";
                    const Param& p = fn->params[i];
                    if (p.isVariadic){
                        result += "__VoxVariadic...";
                    } else {
                        if (p.isRef) result += mapTypeToC(p.type) + "&";
                        else         result += mapTypeToC(p.type);
                    }
                }
                result += ");\n";
            }
        }
        result += "\n";

        // ---- 最后输出所有顶层函数定义 ----
        for (const auto& stmt : statements){
            if (auto fn = std::dynamic_pointer_cast<FunctionDeclNode>(stmt)){
                result += fn->toC();
                result += "\n\n";
            }
        }

        // ---- main ----
        result += "int main() {\n";
        result += "    vox::vox_init_console();\n";
        result += "    try {\n";
        for (const auto& stmt : statements){
            if (stmt->isHeaderInclude()) continue;

            // 跳过所有"定义类"节点
            if (std::dynamic_pointer_cast<FunctionDeclNode>(stmt)) continue;
            if (std::dynamic_pointer_cast<ClassNode>(stmt)) continue;
            if (std::dynamic_pointer_cast<NamespaceNode>(stmt)) continue;
            if (std::dynamic_pointer_cast<ExternBlockNode>(stmt)) continue;
            if (std::dynamic_pointer_cast<ImportNode>(stmt)) continue;
            if (std::dynamic_pointer_cast<SemicolonNode>(stmt)) continue;
            if (std::dynamic_pointer_cast<EndNode>(stmt)) continue;
            if (std::dynamic_pointer_cast<EnumNode>(stmt)) continue;

            // 全局变量：在 main 里手动生成"赋值"
            if (auto var = std::dynamic_pointer_cast<VariableDeclNode>(stmt)){
                if (var->isGlobal){
                    for (std::size_t i = 0; i < var->names.size(); ++i){
                        result += "        " + var->names[i];
                        if (i < var->values.size() && var->values[i]){
                            result += " = " + var->values[i]->toC();
                        }
                        result += ";\n";
                    }
                    continue;
                }
            }

            std::string code = stmt->toC();
            if (code.empty()) continue;
            result += "        " + code + "\n";
        }
        result += "    } catch (const vox::Exception& e) {\n";
        result += "        std::cout << e.exception() << std::endl;\n";
        result += "        return 1;\n";
        result += "    }\n";
        result += "    return 0;\n";
        result += "}\n";

        return result;
    }
};

#endif
