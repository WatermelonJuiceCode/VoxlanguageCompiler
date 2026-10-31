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

// ★ 全局记录所有的抽象类和接口
inline std::set<std::string>& vox_abstract_classes() {
    static std::set<std::string> s;
    return s;
}

// ===================== mapTypeToC =====================
inline std::string mapTypeToC(const std::string& typeName){
    if (typeName.empty()) return "vox::Any";

    // ★★★ 必须放在最前面：优先解析 T? 语法 ★★★
    if (typeName.back() == '?') {
        std::string inner = typeName.substr(0, typeName.size() - 1);
        return "std::optional<" + mapTypeToC(inner) + ">";
    }

    // ★ 如果是抽象类或接口，自动映射为智能指针！
    if (vox_abstract_classes().count(typeName)) {
        return "std::shared_ptr<" + typeName + ">";
    }

    // ---- 基础类型：用 C++ 原生类型 ----
    if (typeName == "bool")   return "bool";
    if (typeName == "char")   return "char";
    if (typeName == "int")    return "int";
    if (typeName == "void")   return "void";
    if (typeName == "any")    return "vox::Any";
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
    if (typeName == "byte")   return "uint8_t";

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

// 根据类型生成"空值"的字面量（★ 前移到 NullNode 之前）
inline std::string nullAsType(const std::string& typeName){
    // ★ T? 的空值是 nullopt
    if (!typeName.empty() && typeName.back() == '?') {
        return "std::nullopt";
    }

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
    if (typeName == "byte")   return "0";
    // 自定义类 / 容器：默认构造
    return mapTypeToC(typeName) + "()";
}

class NullNode : public AstNode {
public:
    // ★ 由 Parser 在 typeCheck 阶段填充
    std::string explicitType;

    std::string toString() const override { return "null"; }
    std::string toC() const override {
        if (!explicitType.empty()){
            return nullAsType(explicitType);   // 精确空值："" / 0 / false / T()
        }
        return "{}";   // 兜底（C++ 空列表初始化）
    }
};

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

class RefArgNode : public AstNode {
public:
    AstNodePtr target;
    explicit RefArgNode(AstNodePtr t) : target(std::move(t)) {}

    std::string toString() const override {
        return "Ref(" + (target ? target->toString() : "") + ")";
    }
    std::string toC() const override {
        return target ? target->toC() : "";
    }
    bool needsStringInclude() const override {
        return target ? target->needsStringInclude() : false;
    }
};

class VariableNode : public AstNode{
public:
    std::string name;
    int line = 0;
    bool needsDeref = false;
    bool forceNoDeref = false;
    bool isOptionalLoopVar = false;
    bool isGlobal = false;              // ★ 是否引用全局变量
    bool isHeapCaptured = false;     // ★ 是否被 ref lambda 捕获（已堆提升）

    explicit VariableNode(std::string argName, int srcLine = 0)
        : name(std::move(argName)), line(srcLine) {}

    std::string toString() const override { return name; }
    std::string toC() const override {
        std::string n = name;
        if (isGlobal &&
            n.find("::") == std::string::npos &&
            n.compare(0, 6, "vox_g_") != 0){
            n = "vox_g_" + n;
        }
        if (needsDeref && !forceNoDeref){
            return "vox::vox_opt_get(" + n + ")";
        }
        if (isHeapCaptured && !isGlobal) {
            return "(*" + n + ")";   // ★ 堆提升后解引用
        }
        return n;
    }
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
    bool isAppendSelf = false;
    std::string toString() const override { return "Self"; }
    std::string toC() const override { 
        return isAppendSelf ? "other" : "(*this)";
    }
};

class OtherNode : public AstNode {
public:
    std::string toString() const override { return "Other"; }
    std::string toC() const override { return "other"; }
};

class SuperNode : public AstNode {
public:
    std::string superClass;
    bool isAppendSuper = false;   // ★ 是否在 append 里
    explicit SuperNode(std::string base = "") : superClass(std::move(base)) {}
    std::string toString() const override { return "Super"; }
    std::string toC() const override { return "(*this)"; }
};

class ListLiteralNode : public AstNode {
public:
    std::vector<AstNodePtr> items;
    explicit ListLiteralNode(std::vector<AstNodePtr> i) : items(std::move(i)) {}
    std::string targetType;   // ★ 由 Parser 填充预期的目标类型
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
        // ★ 空字面量 → C++ 空初始化器（C++ 自动推导目标类型）
        if (items.empty()) {
            return "{}";
        }

        // 提取容器类型 / 元素类型
        std::string container = "vox::List";
        std::string innerVox;
        if (!targetType.empty() && targetType.back() == '>') {
            if (targetType.compare(0, 10, "FinalList<") == 0) {
                container = "vox::FinalList";
                innerVox = targetType.substr(10, targetType.size() - 11);
            } else if (targetType.compare(0, 5, "List<") == 0) {
                container = "vox::List";
                innerVox = targetType.substr(5, targetType.size() - 6);
            }
        }
        std::string inner = innerVox.empty() ? "vox::Any" : mapTypeToC(innerVox);

        // ★ 如果元素类型是 shared_ptr（抽象类多态），用 add 循环构造
        //   因为 List::add 有 make_shared 特化，而 initializer_list 没有
        if (inner.rfind("std::shared_ptr<", 0) == 0) {
            std::string r = "([]() { " + container + "<" + inner + "> _vox_l;";
            for (const auto& item : items) {
                r += " _vox_l.add(" + (item ? item->toC() : "vox::Any()") + ");";
            }
            r += " return _vox_l; })()";
            return r;
        }

        // 普通情况：initializer_list 直接构造
        std::string r = container + "<" + inner + ">{";
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i > 0) r += ", ";
            r += items[i] ? items[i]->toC() : "vox::Any()";
        }
        r += "}";
        return r;
    }

    bool needsStringInclude() const override {
        for (const auto& x : items) if (x->needsStringInclude()) return true;
        return false;
    }
};

class MemberAccessNode : public AstNode {
public:
    AstNodePtr object;
    std::string member;
    bool namespaceAccess;
    bool isPointerAccess = false; // ★ 是不是智能指针访问
    int line = 0;
    int col = 0;

    MemberAccessNode(AstNodePtr obj, std::string mem, bool ns = false, int srcLine = 0, int srcCol = 0)
        : object(std::move(obj)), member(std::move(mem)), namespaceAccess(ns), line(srcLine), col(srcCol) {}

    std::string toString() const override { return object->toString() + "." + member; }

    std::string toC() const override {
        if (auto sn = std::dynamic_pointer_cast<SuperNode>(object)){
            if (sn->isAppendSuper){
                return "other." + sn->superClass + "::" + member;   // ★ append 里带 other
            }
            return sn->superClass + "::" + member;
        }
        if (namespaceAccess){
            return object->toC() + "::" + member;
        }
        
        // ★ 如果标记了是指针访问，使用 ->
        if (isPointerAccess) {
            return object->toC() + "->" + member;
        }

        if (std::dynamic_pointer_cast<StringNode>(object)){
            return "vox::String(" + object->toC() + ")." + member;
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
    std::string op = "=";         // "=" / "+=" / "-="
    AstNodePtr value;
    int line = 0;
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
        // 逗号表达式 —— 任何"单表达式"上下文都能用
        if (assigns.empty()){
            return finalExpr ? finalExpr->toC() : "0";
        }
        std::string r = "(";
        bool first = true;
        for (const auto& p : assigns){
            if (!first) r += ", ";
            first = false;
            r += "(" + p.target->toC() + " " + p.op + " " + p.value->toC() + ")";
        }
        if (finalExpr){
            if (!first) r += ", ";
            r += finalExpr->toC();
        }
        r += ")";
        return r;
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
    bool useAnyOps = false;

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

// ★ 后置自增 / 自减：i++ / i--，返回旧值
class PostIncrementNode : public AstNode {
public:
    AstNodePtr target;      // 被自增/自减的表达式（通常是变量）
    bool isDecrement;       // false = ++，true = --
    int line = 0;

    PostIncrementNode(AstNodePtr t, bool dec, int srcLine = 0)
        : target(std::move(t)), isDecrement(dec), line(srcLine) {}

    std::string toString() const override {
        return std::string("Post") + (isDecrement ? "Dec(" : "Inc(")
             + (target ? target->toString() : "") + ")";
    }

    std::string toC() const override {
        // C++ 后置 ++ / -- 天生返回旧值
        return "(" + (target ? target->toC() : "") + (isDecrement ? "--" : "++") + ")";
    }

    bool needsStringInclude() const override {
        return target ? target->needsStringInclude() : false;
    }
};

class CallNode : public AstNode {
public:
    AstNodePtr callee;
    std::vector<AstNodePtr> args;
    std::vector<std::string> templateArgs;
    std::vector<bool> byRefArgs;
    int line = 0;
    bool userInvoke = false;   // 是否走 _call_
    bool expandSingleListAsVariadic = false;   // ★ 单 List 字面量展开成 variadic
    bool isSignalCall = false;   // ★ 信号触发：e(args) → e.emit(args)
    bool isInstanceSignalCall = false;   // character.e() → character.e.emit()
    std::vector<bool> autoBoxArgs;       // ★ 记录哪些参数需要 make_shared
    bool resolvedParams = false;   // ★ 记录是否已经处理过参数重排
    bool isAppendCall = false;       // ★ 是追加方法调用 → 生成自由函数调用
    std::string appendOwnerClass;    // ★ 宿主类短名
    std::string appendMethodName;    // ★ 追加方法名

    CallNode(AstNodePtr c, std::vector<AstNodePtr> a, int srcLine = 0,
             std::vector<std::string> tArgs = {})
        : callee(std::move(c)), args(std::move(a)),
          templateArgs(std::move(tArgs)), line(srcLine), 
          autoBoxArgs(a.size(), false), resolvedParams(false) {}

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
        // ★ 追加方法调用 → 改写成自由函数 Player_heal(other, args...)
        if (isAppendCall){
            std::string hostExpr;
            if (auto m = std::dynamic_pointer_cast<MemberAccessNode>(callee)){
                hostExpr = m->object->toC();    // self → "other"（isAppendSelf），player → "vox_g_player"
            } else {
                hostExpr = "other";             // 裸调用 heal()，隐式用 other
            }
            std::string call = appendOwnerClass + "_" + appendMethodName + "(" + hostExpr;
            for (const auto& a : args) call += ", " + a->toC();
            call += ")";
            return call;
        }

        std::string pre;
        std::vector<std::string> realArgs;

        if (expandSingleListAsVariadic && args.size() == 1){
            if (auto ll = std::dynamic_pointer_cast<ListLiteralNode>(args[0])){
                for (const auto& item : ll->items){
                    realArgs.push_back(item->toC());
                }
            } else {
                realArgs.push_back(args[0]->toC());
            }
        } else {
            for (const auto& a : args){
                realArgs.push_back(a->toC());
            }
        }

        std::string call;

        std::string calleeStr = callee->toC();
        if (!templateArgs.empty()){
            calleeStr += "<";
            for (std::size_t i = 0; i < templateArgs.size(); ++i){
                if (i > 0) calleeStr += ", ";
                if (templateArgs[i].empty()){
                    calleeStr += "vox::Any";
                } else {
                    calleeStr += mapTypeToC(templateArgs[i]);
                }
            }
            calleeStr += ">";
        }

        if (userInvoke){
            call = "vox::vox_invoke(" + calleeStr;
            for (const auto& a : realArgs) call += ", " + a;
            call += ")";
        } else {
            call = calleeStr + "(";
            for (std::size_t i = 0; i < realArgs.size(); ++i){
                if (i > 0) call += ", ";
                call += realArgs[i];
            }
            call += ")";
        }

        if (isInstanceSignalCall){
            call = calleeStr + ".emit(";
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
            if (auto r = std::dynamic_pointer_cast<RefArgNode>(a)){
                if (r->target && r->target->needsStringInclude()) return true;
            } else if (a->needsStringInclude()) return true;
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
        // ★ 如果 expr 是裸类名（含 ::，比如 vox_mod_Code_edit::Range），
        //   说明用户在 typeof 里写的是"类名"而不是"值" → 直接返回短名
        if (auto v = std::dynamic_pointer_cast<VariableNode>(expr)){
            const std::string& nm = v->name;
            if (nm.find("::") != std::string::npos){
                std::string last = nm;
                auto pos = last.rfind("::");
                if (pos != std::string::npos) last = last.substr(pos + 2);
                return "\"" + last + "\"";
            }
        }
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

    std::string forwardDecl() const override {
        // C++ 里枚举前向声明必须指定底层类型
        return "enum class " + name + " : int;";
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
    bool isMakeShared = false;

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
        std::string r;
        if (isMakeShared) r += "std::make_shared<" + mapTypeToC(className) + ">(";
        else              r += mapTypeToC(className) + "(";
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
        // ★ 无参容器构造：List<int>() / Dict<K,V>() / 用户类()
        if (!expr) {
            return mapTypeToC(targetType) + "()";
        }

        // string(count, char) 特例
        if (targetType == "string" && extraArg){
            return "vox::String(" + expr->toC() + ", " + extraArg->toC() + ")";
        }

        if (targetType == "string") return "vox::vox_str(" + expr->toC() + ")";
        if (targetType == "int")    return "vox::vox_cast_int(" + expr->toC() + ")";
        if (targetType == "int64")  return "vox::vox_cast_int64(" + expr->toC() + ")";
        if (targetType == "int8")   return "static_cast<int8_t>(vox::vox_cast_int(" + expr->toC() + "))";
        if (targetType == "int16")  return "static_cast<int16_t>(vox::vox_cast_int(" + expr->toC() + "))";
        if (targetType == "int32")  return "static_cast<int32_t>(vox::vox_cast_int(" + expr->toC() + "))";
        if (targetType == "uint8")  return "static_cast<uint8_t>(vox::vox_cast_int(" + expr->toC() + "))";
        if (targetType == "uint16") return "static_cast<uint16_t>(vox::vox_cast_int(" + expr->toC() + "))";
        if (targetType == "uint32") return "static_cast<uint32_t>(vox::vox_cast_int(" + expr->toC() + "))";
        if (targetType == "uint64") return "static_cast<uint64_t>(vox::vox_cast_int64(" + expr->toC() + "))";
        if (targetType == "bool")   return "vox::vox_cast_bool(" + expr->toC() + ")";
        if (targetType == "char")   return "vox::vox_cast_char(" + expr->toC() + ")";
        if (targetType == "float")  return "vox::vox_cast_float(" + expr->toC() + ")";
        if (targetType == "double") return "vox::vox_cast_double(" + expr->toC() + ")";

        return "static_cast<" + mapTypeToC(targetType) + ">(" + expr->toC() + ")";
    }

    bool needsStringInclude() const override {
        if (expr && expr->needsStringInclude()) return true;
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
    std::string name;                        // 例："test"
    std::vector<AstNodePtr> args;            // 例：{NumberNode(10)}
    int line = 0;
};

class BinaryNode : public AstNode {
    public:
    AstNodePtr left;
    TokenType op;
    AstNodePtr right;
    int line = 0;
    bool useAnyOps = false;   // ★ 任一侧是 any → 走 vox_any_* 函数
    bool isStringConcat = false;   // ★ Parser 语义分析阶段设置
    
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

        // ★ Any 运算：走运行时函数
        if (useAnyOps){
            std::string fn;
            switch (op){
            case TokenType::Plus:  fn = "vox::vox_any_add"; break;
            case TokenType::Sub:   fn = "vox::vox_any_sub"; break;
            case TokenType::Mul:   fn = "vox::vox_any_mul"; break;
            case TokenType::Div:   fn = "vox::vox_any_div"; break;
            case TokenType::Mod:   fn = "vox::vox_any_mod"; break;
            default: break;
            }
            if (!fn.empty()){
                return fn + "(" + left->toC() + ", " + right->toC() + ")";
            }
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
                // ★ 两侧都不是字面量，但 Parser 标记了"这是字符串拼接"
                //   必须两侧都显式 vox_str，避免 String + int 歧义
                if (isStringConcat){
                    return "vox::vox_str(" + l + ") + vox::vox_str(" + r + ")";
                }
            }
        }
        // ★ && / || 两侧用 vox_truthy 包裹，兼容 optional / Any / 自定义 _bool_
        if (op == TokenType::AmpAmp){
            return "(vox::vox_truthy(" + left->toC() + ") && "
                 + "vox::vox_truthy(" + right->toC() + "))";
        }
        if (op == TokenType::PipePipe){
            return "(vox::vox_truthy(" + left->toC() + ") || "
                 + "vox::vox_truthy(" + right->toC() + "))";
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

class DictLiteralNode : public AstNode {
public:
    std::vector<AstNodePtr> keys;
    std::vector<AstNodePtr> values;
    std::string targetType;   // ★ 目标类型

    DictLiteralNode(std::vector<AstNodePtr> k, std::vector<AstNodePtr> v)
        : keys(std::move(k)), values(std::move(v)) {}

    // ★ 必须实现 toString，否则是抽象类
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
        // ★ 空字面量 → C++ 空初始化
        if (keys.empty() && values.empty()) {
            return "{}";
        }

        // ★ 根据 targetType 决定容器类型（Dict / FinalDict）
        std::string container = "vox::Dict";
        std::string ktVox = "vox::Any", vtVox = "vox::Any";

        if (!targetType.empty() && targetType.back() == '>') {
            std::string innerVox;
            if (targetType.compare(0, 10, "FinalDict<") == 0) {
                container = "vox::FinalDict";
                innerVox = targetType.substr(10, targetType.size() - 11);
            } else if (targetType.compare(0, 5, "Dict<") == 0) {
                container = "vox::Dict";
                innerVox = targetType.substr(5, targetType.size() - 6);
            }

            if (!innerVox.empty()) {
                // 按顶层逗号分割两个类型参数
                std::vector<std::string> parts;
                int depth = 0;
                std::size_t start = 0;
                for (std::size_t i = 0; i < innerVox.size(); ++i) {
                    char c = innerVox[i];
                    if (c == '<' || c == '(') depth++;
                    else if (c == '>' || c == ')') depth--;
                    else if (c == ',' && depth == 0) {
                        parts.push_back(innerVox.substr(start, i - start));
                        start = i + 1;
                    }
                }
                parts.push_back(innerVox.substr(start));
                for (auto& p : parts) {
                    while (!p.empty() && p.front() == ' ') p.erase(p.begin());
                    while (!p.empty() && p.back()  == ' ') p.pop_back();
                }
                if (parts.size() >= 2) {
                    if (!parts[0].empty()) ktVox = parts[0];
                    if (!parts[1].empty()) vtVox = parts[1];
                }
            }
        }

        std::string kt = mapTypeToC(ktVox);
        std::string vt = mapTypeToC(vtVox);

        std::string r = container + "<" + kt + ", " + vt + ">{";
        for (std::size_t i = 0; i < keys.size() && i < values.size(); ++i) {
            if (i > 0) r += ", ";
            r += "{" + (keys[i]   ? keys[i]->toC()   : "vox::Any()") + ", "
                    + (values[i] ? values[i]->toC() : "vox::Any()") + "}";
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
        std::string pre;                 
        std::string result;

        if (isConst) result += "const ";

        // ★ 新增堆提升类型
        if (isHeapCaptured && !isGlobal){
            result += "std::shared_ptr<" + baseType + ">";
        } else {
            result += baseType;
        }

        for (std::size_t i = 0; i < names.size(); ++i){
            if (i == 0) result += " ";
            else        result += ", ";
            
            if (isGlobal) result += "vox_g_" + names[i];
            else          result += names[i];
            
            if (i < values.size() && values[i]){
                if (auto ae = std::dynamic_pointer_cast<AssignExprNode>(values[i])){
                    for (const auto& p : ae->assigns){
                        pre += p.target->toC() + " " + p.op + " "
                            + p.value->toC() + ";\n";
                    }
                    std::string v = ae->finalExpr ? ae->finalExpr->toC() : "0";
                    if (isHeapCaptured && !isGlobal) {
                        result += " = std::make_shared<" + baseType + ">(" + v + ")";
                    } else {
                        result += " = " + v;
                    }
                } else {
                    std::string valC = values[i]->toC();
                    if (isHeapCaptured && !isGlobal) {
                        result += " = std::make_shared<" + baseType + ">(" + valC + ")";
                    } else {
                        result += " = " + valC;
                    }
                }
            } else if (isHeapCaptured && !isGlobal) {
                result += " = std::make_shared<" + baseType + ">()";
            }
        }
        result += ";";
        return pre + result;
    }
    bool needsStringInclude() const override {
        if (typeName == "string") return true;
        for (const auto& v : values){
            if (v && v->needsStringInclude()) return true;
        }
        return false;
    }

    // 告诉 ProgramNode 这个节点应该放在 main 外面
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
        // ★ 用 () 而非 {}：GCC 对"花括号 + 变参模板构造"有误判聚合体的怪癖，
        //   圆括号会强制走构造函数重载，避开 most vexing parse 用 (void) 前缀解决
        std::string r = "(void)" + className + "(" + varName;
        for (const auto& a : args) r += ", " + a->toC();
        r += ");";
        return r;
    }
    bool needsStringInclude() const {
        for (const auto& a : args) if (a && a->needsStringInclude()) return true;
        return false;
    }
};

// ★ 组合限定：多个 Constraint 按 & 连接
struct ConstraintSet {
    std::vector<Constraint> items;

    std::string toCCheck(const std::string& varName) const {
        std::string r;
        for (std::size_t i = 0; i < items.size(); ++i){
            if (i > 0) r += "\n";
            r += items[i].toCCheck(varName);
        }
        return r;
    }

    bool needsStringInclude() const {
        for (const auto& c : items){
            if (c.needsStringInclude()) return true;
        }
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
    std::optional<ConstraintSet> constraint;    // ★
    bool isAnyTarget = false;   // ★ LHS 是 any 类型
    bool isHeapCaptured = false;   // ★ 被 ref lambda 捕获并堆提升

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
        
        // ★ 如果这个是堆提升的变量，要解引用
        if (!target && isHeapCaptured) {
            lhs = "(*" + lhs + ")";
        }

        std::string pre;
        std::string rhsC;
        if (value){
            if (auto ae = std::dynamic_pointer_cast<AssignExprNode>(value)){
                for (const auto& p : ae->assigns){
                    pre += p.target->toC() + " " + p.op + " "
                         + p.value->toC() + ";\n";
                }
                rhsC = ae->finalExpr ? ae->finalExpr->toC() : "0";
            } else {
                // ★ 直接取值，不需要任何额外包装
                rhsC = value->toC();
            }
        }

        if (constraint.has_value()){
            std::string tmp = "_vox_tmp_" + std::to_string(line) + "_" + name;
            std::string body;
            if (isCompound && !compoundOp.empty()){
                body = "{ auto " + tmp + " = " + lhs + "; "
                    + tmp + " " + compoundOp + " " + rhsC
                    + "; " + constraint->toCCheck(tmp)
                    + " " + lhs + " = " + tmp + "; }";
            } else {
                body = "{ auto " + tmp + " = " + rhsC + "; "
                    + constraint->toCCheck(tmp)
                    + " " + lhs + " = " + tmp + "; }";
            }
            return pre + body;
        }
        if (isCompound && !compoundOp.empty()){
            return pre + lhs + " " + compoundOp + " " + rhsC + ";";
        }
        return pre + lhs + " = " + rhsC + ";";
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


struct Param {
    std::string type;
    std::string name;
    bool isRef = false;
    bool isVariadic = false;
    bool afterSlash = false;
    bool isPosOnly = false;
    bool isKwOnly = false;
    bool isConst = false;
    AstNodePtr defaultValue;
    std::optional<ConstraintSet> constraint;    // ★
};

// ===================== signal =====================
class SignalDeclNode : public AstNode {
public:
    std::string name;              // 短名（不含 namespace）
    std::vector<Param> params;
    int line = 0;

    SignalDeclNode(std::string n, std::vector<Param> p, int srcLine = 0)
        : name(std::move(n)), params(std::move(p)), line(srcLine) {}

    std::string toString() const override {
        std::string r = "Signal(" + name + "(";
        for (std::size_t i = 0; i < params.size(); ++i){
            if (i > 0) r += ", ";
            r += params[i].type + " " + params[i].name;
        }
        r += "))";
        return r;
    }

    std::string toC() const override {
        std::string sigType = "vox::Signal<";
        for (std::size_t i = 0; i < params.size(); ++i){
            if (i > 0) sigType += ", ";
            sigType += mapTypeToC(params[i].type);
        }
        sigType += ">";
        return "inline " + sigType + " " + name + ";";
    }

    bool isTopLevelDefinition() const override { return true; }
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
    bool isConst = false;
    std::optional<ConstraintSet> constraint;   // ★
};

class LambdaNode : public AstNode {
public:
    std::string returnType;
    std::vector<LambdaParam> params;
    std::shared_ptr<BlockNode> body;
    bool hasHeapCapture = false;   // ★ 是否包含被堆提升的捕获
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
        // ★ 如果有堆捕获，必须用 [=] 按值拷贝 shared_ptr
        std::string captureStr = byRef ? "&" : "=";
        if (byRef && hasHeapCapture) {
            captureStr = "=";
        }

        std::string r = "[" + captureStr + "](";
        for (std::size_t i = 0; i < params.size(); ++i){
            if (i > 0) r += ", ";
            if (params[i].isConst) r += "const ";
            r += mapTypeToC(params[i].type) + " " + params[i].name;
        }
        r += ") -> " + mapTypeToC(returnType) + " {\n";
        for (const auto& p : params){
            if (p.constraint.has_value()){
                r += "    " + p.constraint->toCCheck(p.name) + "\n";
            }
        }
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
    bool inMain = false;

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

    std::string toC() const {
        bool thenIsString = std::dynamic_pointer_cast<StringNode>(thenExpr) != nullptr;
        bool elseIsString = std::dynamic_pointer_cast<StringNode>(elseExpr) != nullptr;

        if (thenIsString && !elseIsString){
            // 两分支都转成 String
            return "(vox::vox_truthy(" + condition->toC() + ") ? vox::String(" + thenExpr->toC() + ")"
                + " : vox::vox_str(" + elseExpr->toC() + "))";
        }
        if (elseIsString && !thenIsString){
            return "(vox::vox_truthy(" + condition->toC() + ") ? vox::vox_str(" + thenExpr->toC() + ")"
                + " : vox::String(" + elseExpr->toC() + "))";
        }
        // 都不是 / 都是字符串 → 原逻辑
        return "(" + condition->toC() + " ? " + thenExpr->toC() + " : " + elseExpr->toC() + ")";
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
        // ★ 如果 condition 是 AssignExprNode → 翻译成 for 循环
        //   让 init 只执行一次
        if (auto ae = std::dynamic_pointer_cast<AssignExprNode>(condition)){
            std::string init;
            for (const auto& p : ae->assigns){
                if (!init.empty()) init += ", ";
                init += p.target->toC() + " " + p.op + " " + p.value->toC();
            }
            std::string r = "for (" + init + "; "
                          + (ae->finalExpr ? ae->finalExpr->toC() : "true")
                          + "; ) {\n";
            if (body) r += body->toC();
            r += "}";
            return r;
        }

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
        // ★ 如果 condition 是 AssignExprNode → init 提到循环外，只执行一次
        if (auto ae = std::dynamic_pointer_cast<AssignExprNode>(condition)){
            std::string r = "{\n";
            for (const auto& p : ae->assigns){
                r += "    " + p.target->toC() + " " + p.op + " "
                   + p.value->toC() + ";\n";
            }
            r += "    do {\n";
            if (body) r += body->toC();
            r += "    } while ("
               + (ae->finalExpr ? ae->finalExpr->toC() : "false")
               + ");\n";
            r += "}";
            return r;
        }

        // 原逻辑
        std::string r = "do {\n";
        if (body) r += body->toC();
        r += "} while (" + (condition ? condition->toC() : "false") + ");";
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
            // ★ 不要用 vox_deref！
            r += "        auto&& " + vars[0].name
            + " = __vox_it_0._current_();\n";
            r += "        __vox_it_0._next_();\n";
        } else {
            // 多变量：也恢复成 std::optional<T> 的原始逻辑
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

class NullCoalesceNode : public AstNode {
public:
    AstNodePtr left;
    AstNodePtr right;
    int line = 0;

    NullCoalesceNode(AstNodePtr l, AstNodePtr r, int srcLine = 0) 
        : left(std::move(l)), right(std::move(r)), line(srcLine) {}

    std::string toString() const override {
        return "NullCoalesce(" + left->toString() + ", " + right->toString() + ")";
    }
    std::string toC() const override {
        // 生成 C++ 代码
        return "vox::vox_coalesce(" + left->toC() + ", " + right->toC() + ")";
    }
    bool needsStringInclude() const override { 
        return (left && left->needsStringInclude()) || (right && right->needsStringInclude()); 
    }
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
    std::vector<Attribute> attributes;    // ★ 装饰器
    std::string returnType;
    std::string name;
    std::vector<std::string> subscribesTo;     // ★ 订阅的信号全名（可能含 "::"）
    std::vector<std::string> resolvedTargets;   // Parser 填好的 C++ 表达式
    std::vector<Param> params;
    std::shared_ptr<BlockNode> body;
    bool isExtern;
    bool isModify = false;   // ★ 标记是否被 modify 修饰
    int line = 0;
    std::vector<std::string> templateParams;   // ★ 模板参数名列表（空表示非模板）

    // ★ 装饰器适配信息（Parser 在 typeCheckStmt 阶段填充）
    struct DecoratorAdapt {
        bool decParamIsAny = false;    // 首参是 any（vox::Any 装箱）
        std::string decParamRet;       // 首参是零参函数时的返回类型
        std::string decoratorRet;      // 装饰器本身返回类型
        bool castReturn = false;       // wrapper 需要拆装饰器返回的 Any
        std::string castTo;            // cast 的目标类型（= wrapper returnType）
    };
    DecoratorAdapt decoratorAdapt;

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

    // 生成指定名字的完整函数代码
    std::string generateFuncCode(const std::string& funcName) const {
        // ============ extern：只生成声明 ============
        if (isExtern){
            std::string templateHeader;
            if (!templateParams.empty()){
                templateHeader = "template <";
                for (std::size_t i = 0; i < templateParams.size(); ++i){
                    if (i > 0) templateHeader += ", ";
                    templateHeader += "typename " + templateParams[i];
                }
                templateHeader += ">\n";
            }

            std::string result = templateHeader;
            result += mapTypeToC(returnType) + " " + funcName + "(";
            for (std::size_t i = 0; i < params.size(); ++i){
                if (i > 0) result += ", ";
                if (params[i].isConst) result += "const ";
                if (params[i].isRef)   result += mapTypeToC(params[i].type) + "& ";
                else                   result += mapTypeToC(params[i].type) + " ";
                result += params[i].name;
            }
            result += ");";
            return result;
        }

        // ============ 普通函数：生成定义 ============
        bool hasVariadic = false;
        for (const auto& p : params){
            if (p.isVariadic){ hasVariadic = true; break; }
        }

        // 组装 template 头
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

        // ---------- 变参 ----------
        if (hasVariadic){
            result += mapTypeToC(returnType) + " " + funcName + "(";

            bool first = true;
            for (const auto& p : params){
                if (p.isVariadic) continue;
                if (!first) result += ", ";
                first = false;
                if (p.isConst) result += "const ";
                if (p.isRef)   result += mapTypeToC(p.type) + "& ";
                else           result += mapTypeToC(p.type) + " ";
                result += p.name;
                // ★ 补上默认值
                if (p.defaultValue){
                    if (std::dynamic_pointer_cast<NullNode>(p.defaultValue)){
                        result += " = " + nullAsType(p.type);
                    } else {
                        result += " = " + p.defaultValue->toC();
                    }
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
                    result += "    vox::List<" + mapTypeToC(p.type) + "> " + p.name + ";\n";
                    if (p.type == "string"){
                        result += "    (" + p.name + ".add(vox::vox_str(__vox_var_" + p.name + ")), ...);\n";
                    } else {
                        result += "    (" + p.name + ".add(__vox_var_" + p.name + "), ...);\n";
                    }
                }
            }
            for (const auto& p : params){
                if (!p.isVariadic && p.constraint.has_value()){
                    result += "    " + p.constraint->toCCheck(p.name) + "\n";
                }
            }
            result += body->toC();
            result += "}";
        }
        // ---------- 普通 ----------
        else {
            result += mapTypeToC(returnType) + " " + funcName + "(";
            for (std::size_t i = 0; i < params.size(); ++i){
                if (i > 0) result += ", ";
                if (params[i].isConst) result += "const ";
                if (params[i].isRef)   result += mapTypeToC(params[i].type) + "& ";
                else                   result += mapTypeToC(params[i].type) + " ";
                result += params[i].name;
                // ★ 补上默认值
                if (params[i].defaultValue){
                    if (std::dynamic_pointer_cast<NullNode>(params[i].defaultValue)){
                        result += " = " + nullAsType(params[i].type);
                    } else {
                        result += " = " + params[i].defaultValue->toC();
                    }
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

    // 生成参数字符串（如 "int a, string b"）
    std::string generateParamString() const {
        std::string res;
        for (size_t i = 0; i < params.size(); ++i) {
            if (i > 0) res += ", ";
            if (params[i].isRef) res += mapTypeToC(params[i].type) + "& ";
            else                 res += mapTypeToC(params[i].type) + " ";
            res += params[i].name;
        }
        return res;
    }

    std::string toC() const override {
        if (isExtern || attributes.empty()) {
            return generateFuncCode(name);
        }

        std::string implName = name + "_vox_impl_" + std::to_string(line);
        std::string implCode = generateFuncCode(implName);
        std::string decName  = attributes[0].name;

        // ============ 生成捕获 wrapper 参数的零参 thunk ============
        std::string capture, callArgs;
        for (std::size_t i = 0; i < params.size(); ++i) {
            if (i > 0) { capture += ", "; callArgs += ", "; }
            capture  += params[i].name;
            callArgs += params[i].name;
        }
        std::string implCall = implName + "(" + callArgs + ")";

        // lambda 的返回类型：
        //   - 首参是 any → 保留原返回类型（后续包成 std::function）
        //   - 首参是零参函数类型 → 用首参声明的返回类型
        std::string lambdaRet;
        if (decoratorAdapt.decParamIsAny) {
            lambdaRet = mapTypeToC(returnType);
        } else {
            lambdaRet = mapTypeToC(decoratorAdapt.decParamRet);
        }

        // lambda 的 body：根据"首参返回类型"和"impl 返回类型"决定
        std::string body;
        if (returnType == "void") {
            body = implCall + ";";                                    // impl 无返回值
        } else if (!decoratorAdapt.decParamIsAny &&
                decoratorAdapt.decParamRet == "void") {
            body = implCall + ";";                                    // 丢弃返回值
        } else if (!decoratorAdapt.decParamIsAny &&
                decoratorAdapt.decParamRet == "any") {
            body = "return vox::Any(" + implCall + ");";              // 装箱
        } else {
            body = "return " + implCall + ";";                        // 直接返回（含隐式转换）
        }

        std::string lambda = "[" + capture + "]() -> " + lambdaRet +
                            " { " + body + " }";

        // ============ 组装装饰器实参 ============
        std::string adaptedArg;
        if (decoratorAdapt.decParamIsAny) {
            // 首参是 any → 包成 std::function 再装箱 Any
            // （Any 里 IsStdFunction 特判会自动生成 _caller）
            std::string fnType = "std::function<" + mapTypeToC(returnType) + "()>";
            adaptedArg = "vox::Any(" + fnType + "(" + lambda + "))";
        } else {
            // 首参是具体零参函数类型 → 直接传 lambda（隐式转 std::function）
            adaptedArg = lambda;
        }

        // ============ 生成 wrapper ============
        std::string wrapper = mapTypeToC(returnType) + " " + name +
                            "(" + generateParamString() + ") {\n";

        std::string decCall = decName + "(" + adaptedArg;
        for (const auto& arg : attributes[0].args) {
            decCall += ", " + arg->toC();
        }
        decCall += ")";

        if (returnType == "void") {
            wrapper += "    " + decCall + ";\n";
        } else if (decoratorAdapt.castReturn) {
            wrapper += "    return " + anyCastExpr(returnType, decCall) + ";\n";
        } else {
            wrapper += "    return " + decCall + ";\n";
        }
        wrapper += "}\n";

        return implCode + "\n" + wrapper;
    }

    // Any → 具体类型的 cast（与 CastNode::toC 保持一致）
    static std::string anyCastExpr(const std::string& t, const std::string& expr) {
        if (t == "string") return "vox::vox_str(" + expr + ")";
        if (t == "int")    return "vox::vox_cast_int(" + expr + ")";
        if (t == "int64")  return "vox::vox_cast_int64(" + expr + ")";
        if (t == "int8")   return "static_cast<int8_t>(vox::vox_cast_int(" + expr + "))";
        if (t == "int16")  return "static_cast<int16_t>(vox::vox_cast_int(" + expr + "))";
        if (t == "int32")  return "static_cast<int32_t>(vox::vox_cast_int(" + expr + "))";
        if (t == "uint8")  return "static_cast<uint8_t>(vox::vox_cast_int(" + expr + "))";
        if (t == "uint16") return "static_cast<uint16_t>(vox::vox_cast_int(" + expr + "))";
        if (t == "uint32") return "static_cast<uint32_t>(vox::vox_cast_int(" + expr + "))";
        if (t == "uint64") return "static_cast<uint64_t>(vox::vox_cast_int64(" + expr + "))";
        if (t == "bool")   return "vox::vox_cast_bool(" + expr + ")";
        if (t == "char")   return "vox::vox_cast_char(" + expr + ")";
        if (t == "float")  return "vox::vox_cast_float(" + expr + ")";
        if (t == "double") return "vox::vox_cast_double(" + expr + ")";
        return "static_cast<" + mapTypeToC(t) + ">(" + expr + ")";
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
    bool isPlaceholder = false;                // ★
    std::vector<std::string> internalUsings;
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
        // ★ placeholder：只生成 using namespace，绝不生成 namespace X { }
        if (isPlaceholder){
            return "using namespace " + name + ";\n";
        }

        std::string r;

        if (!skipIncludes){
            std::string includes;
            collectIncludes(includes);
            if (!includes.empty()){
                r += includes + "\n";
            }
        }

        r += "namespace " + name + " {\n";

        for (const auto& u : internalUsings){
            r += "using namespace " + u + ";\n";
        }

        // ★ 先输出所有 class 的前向声明
        for (const auto& s : statements){
            std::string decl = s->forwardDecl();
            if (!decl.empty()){
                r += decl + "\n";
            }
        }

        // ★ 输出所有自由函数的前向声明（同命名空间内互调需要）
        for (const auto& s : statements){
            auto fn = std::dynamic_pointer_cast<FunctionDeclNode>(s);
            if (!fn) continue;
            if (fn->isExtern) continue;
            if (fn->body == nullptr) continue;   // 只有签名、没有定义 → 不需要为它声明

            // ---- template 头 ----
            bool hasVariadic = false;
            for (const auto& p : fn->params){
                if (p.isVariadic){ hasVariadic = true; break; }
            }
            if (!fn->templateParams.empty() || hasVariadic){
                r += "template <";
                bool first = true;
                for (const auto& tp : fn->templateParams){
                    if (!first) r += ", ";
                    first = false;
                    r += "typename " + tp;
                }
                if (hasVariadic){
                    if (!first) r += ", ";
                    r += "typename... __VoxVariadic";
                }
                r += ">\n";
            }

            // ---- 返回类型 + 名字 + 参数类型 ----
            r += mapTypeToC(fn->returnType) + " " + fn->name + "(";
            for (std::size_t i = 0; i < fn->params.size(); ++i){
                if (i > 0) r += ", ";
                const Param& p = fn->params[i];
                if (p.isVariadic){
                    r += "__VoxVariadic...";
                } else {
                    if (p.isConst) r += "const ";
                    if (p.isRef)   r += mapTypeToC(p.type) + "&";
                    else           r += mapTypeToC(p.type);
                }
            }
            r += ");\n";
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
            tryPart += " catch (const vox::" + c.exceptionType + "& " + c.variableName + ") {\n";
            tryPart += c.body->toC();
            tryPart += "}";
        }

        if (!finallyBody){
            return tryPart;
        }

        // ★ 用 {} 包一层，让 finally 里的局部变量不泄漏到外层
        std::string fb = "{\n" + finallyBody->toC() + "}\n";

        // ★ 无 catch：内层不套 try，直接把 tryBody 展开到外层 try
        if (catches.empty()){
            std::string r;
            r += "try {\n";
            if (tryBody) r += tryBody->toC();
            r += "} catch (...) {\n";
            r += fb;
            r += "    throw;\n";
            r += "}\n";
            r += fb;
            return r;
        }

        // ★ 有 catch：双层 try
        std::string r;
        r += "try {\n";
        r += "    " + tryPart + "\n";
        r += "} catch (...) {\n";
        r += fb;
        r += "    throw;\n";
        r += "}\n";
        r += fb;
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
    AstNodePtr whenCond;                    // ★ when 条件
    std::shared_ptr<BlockNode> body;
};

class SwitchNode : public AstNode {
public:
    AstNodePtr expr;
    std::vector<SwitchCase> cases;
    int line = 0;

    SwitchNode(AstNodePtr e, std::vector<SwitchCase> c, int srcLine = 0)
        : expr(std::move(e)), cases(std::move(c)), line(srcLine) {}

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
        std::string r = "{\n";
        r += "    auto _vox_switch_val = " + (expr ? expr->toC() : "0") + ";\n";
        bool first = true;
        const SwitchCase* defaultCase = nullptr;

        for (const auto& c : cases) {
            if (c.value) {
                r += first ? "    if (" : "    else if (";
                first = false;
                
                // 生成值比较
                r += "vox::vox_truthy(_vox_switch_val == " + c.value->toC() + ")";
                
                // 生成 when 条件
                if (c.whenCond) {
                    r += " && vox::vox_truthy(" + c.whenCond->toC() + ")";
                }
                
                r += ") {\n";
                if (c.body) r += c.body->toC();
                r += "    }\n";
            } else {
                defaultCase = &c;
            }
        }

        if (defaultCase) {
            if (defaultCase->whenCond) {
                r += first ? "    if (" : "    else if (";
                r += "vox::vox_truthy(" + defaultCase->whenCond->toC() + ")";
                r += ") {\n";
            } else {
                r += first ? "    {\n" : "    else {\n";
            }
            if (defaultCase->body) r += defaultCase->body->toC();
            r += "    }\n";
        }
        r += "}\n";
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

// ===================== with =====================
class WithNode : public AstNode {
public:
    std::string typeName;                 // 可选类型名（空 = auto）
    std::string varName;                  // 绑定变量名
    AstNodePtr init;                      // 初始化表达式
    std::shared_ptr<BlockNode> body;

    WithNode(std::string t, std::string v, AstNodePtr i, std::shared_ptr<BlockNode> b)
        : typeName(std::move(t)), varName(std::move(v)),
          init(std::move(i)), body(std::move(b)) {}

    std::string toString() const override {
        return "With(" + varName + ") { "
             + (body ? body->toString() : "") + " }";
    }

    std::string toC() const override {
        std::string v = varName;
        std::string guard;
        
        std::string r = "{\n";
        if (!v.empty()) {
            // ★ 显式声明了变量：新建对象
            guard = "_VoxWithGuard_" + v;
            std::string declType = typeName.empty() ? "auto" : mapTypeToC(typeName);
            r += "    " + declType + " " + v + " = " + (init ? init->toC() : "{}") + ";\n";
            r += "    struct " + guard + " {\n";
            r += "        decltype(" + v + ")& _obj;\n";
            r += "        " + guard + "(decltype(" + v + ")& o) : _obj(o) { _obj._startWith_(); }\n";
            r += "        ~" + guard + "() { _obj._endWith_(); }\n";
            r += "    } _wg_" + v + "{" + v + "};\n";
        } else {
            // ★ 没有显式变量名，用 auto&& 引用绑定
            //   如果是 lvalue (如 draw)，不拷贝；如果是 rvalue (如 new Draw())，延长生命周期
            v = "_vox_with_obj";
            guard = "_VoxWithGuard_" + v;
            r += "    auto&& " + v + " = " + (init ? init->toC() : "{}") + ";\n";
            r += "    struct " + guard + " {\n";
            r += "        decltype(" + v + ")& _obj;\n";
            r += "        " + guard + "(decltype(" + v + ")& o) : _obj(o) { _obj._startWith_(); }\n";
            r += "        ~" + guard + "() { _obj._endWith_(); }\n";
            r += "    } _wg_" + v + "{" + v + "};\n";
        }

        if (body) r += body->toC();
        r += "}\n";
        return r;
    }

    bool needsStringInclude() const override {
        if (init && init->needsStringInclude()) return true;
        return body ? body->needsStringInclude() : false;
    }
};

// ===================== 类系统 =====================
struct ClassField {
    std::string access;
    bool isStatic;
    bool isConst = false;
    std::string typeName;
    std::string name;
    AstNodePtr init;
    std::optional<ConstraintSet> constraint;
};

struct ClassMethod {
    std::string access;
    bool isStatic;
    bool isOverride;
    bool isCtor;
    bool isDtor = false;              // ★ _del_
    bool isAbstract = false;         // ★ 纯虚方法
    bool isConst = false;
    bool hasBody = false;
    std::string returnType;
    std::string name;
    std::vector<Param> params;
    std::shared_ptr<BlockNode> body;
    std::vector<std::string> subscribesTo;         // "self.e" / "super.e" / "e"
    std::vector<std::string> subscribesToShort;    // 剥前缀后的短名 "e"
    std::vector<std::vector<Param>> subscribesToParams;
    int line = 0;
};

struct ClassSignal {
    std::string access;
    std::string name;
    std::vector<Param> params;
    int line = 0;
};

class ClassNode : public AstNode {
public:
    std::vector<Attribute> attributes;
    std::string name;
    std::vector<std::string> bases;
    std::vector<bool> baseIsInterface;      // ← 和 bases 一一对应
    std::vector<ClassField> fields;
    std::vector<ClassSignal> signals;
    std::vector<ClassMethod> methods;
    bool isStruct = false;
    bool isInterface = false;
    bool isAbstract = false;         // ★
    bool isCanAppend = false;   // ★ 声明：允许被追加
    bool isAppend = false;      // ★ 声明：这是一个 append 追加块
    int line = 0;                    // ★
    std::vector<std::string> templateParams;   // ★

    bool allowAppendFriend = false;                // friend append;
    std::set<std::string> appendFriendMethods;      // friend heal, restoreMp;
    std::vector<std::string> friendDecls;           // 生成的 C++ friend 声明

    ClassNode() = default;
    ClassNode(std::string n, std::vector<std::string> b, int srcLine = 0)
        : name(std::move(n)), bases(std::move(b)), line(srcLine) {}

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
        // ★ 是否任意字段带约束 —— 必须在所有分支之前声明
        bool anyFieldConstraint = false;
        for (const auto& f : fields){
            if (f.constraint.has_value()){ anyFieldConstraint = true; break; }
        }

        // ========== interface：纯虚类 ==========
        if (isInterface) {
            std::string r;
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
                    if (isIface) r += "virtual public " + bases[i];
                    else         r += "public " + bases[i];
                }
            }
            r += " {\npublic:\n";
            r += "    virtual ~" + name + "() = default;\n\n";
            r += "    " + name + "(vox::NoInit) {}\n\n";
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
            r += "\n    virtual vox::String vox_type_name() const { return \"" + name + "\"; }\n";
            r += "    virtual bool vox_is_type(const vox::String& t) const {\n";
            r += "        if (t == \"" + name + "\") return true;\n";
            for (std::size_t i = 0; i < bases.size(); ++i){
                if ((i < baseIsInterface.size()) ? baseIsInterface[i] : true) {
                    r += "        if (" + bases[i] + "::vox_is_type(t)) return true;\n";
                }
            }
            r += "        return false;\n    }\n};\n";
            return r;
        }

        // ★ append 类 → 生成自由函数（宿主参数名就叫 other）
        if (isAppend) {
            std::string r;
            auto emitSignature = [&](const ClassMethod& m, bool withDefaults){
                std::string rt = m.returnType.empty() ? "void" : mapTypeToC(m.returnType);
                std::string s = rt + " " + name + "_" + m.name
                              + "(" + mapTypeToC(name) + "& other";
                for (const auto& p : m.params){
                    s += ", ";
                    if (p.isRef) s += mapTypeToC(p.type) + "& ";
                    else         s += mapTypeToC(p.type) + " ";
                    s += p.name;
                    if (withDefaults && p.defaultValue){
                        s += " = " + p.defaultValue->toC();
                    }
                }
                s += ")";
                return s;
            };
            for (const auto& m : methods){
                if (m.isCtor || m.isDtor || m.isOverride) continue;
                r += emitSignature(m, false) + ";\n";
            }
            r += "\n";
            for (const auto& m : methods){
                if (m.isCtor || m.isDtor || m.isOverride) continue;
                r += emitSignature(m, true) + " {\n";
                if (m.body) r += m.body->toC();
                r += "}\n\n";
            }
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
                    r += "virtual public " + bases[i];
                } else {
                    r += "public " + bases[i];
                }
            }
        }
        r += " {\n";

        // ========== public 区域 ==========
        r += "public:\n";

        // ★ 查有没有 _del_
        bool hasDtor = false;
        for (const auto& m : methods){
            if (m.isDtor){ hasDtor = true; break; }
        }
        if (hasDtor){
            r += "    virtual ~" + name + "() { _del_(); }\n\n";
        } else {
            r += "    virtual ~" + name + "() = default;\n\n";
        }

        // ==================== 拷贝与移动语义 ====================
        if (hasDtor) {
            // 独占资源类：禁用拷贝，生成移动
            r += "    " + name + "(const " + name + "&) = delete;\n\n";
            r += "    " + name + "(" + name + "&& other) noexcept {\n";
            for (const auto& f : fields){
                if (f.isStatic) continue;
                r += "        this->" + f.name + " = std::move(other." + f.name + ");\n";
                // ★★★ 核心：强制将源对象置空 ★★★
                r += "        other." + f.name + " = -1;\n"; 
            }
            for (const auto& s : signals){
                r += "        this->" + s.name + " = std::move(other." + s.name + ");\n";
            }
            for (const auto& b : bases){
                r += "        " + b + "::operator=(std::move(other));\n";
            }
            r += "    }\n\n";
            
            r += "    " + name + "& operator=(const " + name + "&) = delete;\n\n";
            r += "    " + name + "& operator=(" + name + "&& other) noexcept {\n";
            r += "        if (this != &other) {\n";
            r += "            _del_();\n"; // 先释放自己的资源
            for (const auto& f : fields){
                if (f.isStatic) continue;
                r += "            this->" + f.name + " = std::move(other." + f.name + ");\n";
                // ★★★ 核心修复：强制将源对象置空 ★★★
                r += "            other." + f.name + " = decltype(other." + f.name + "){};\n"; 
            }
            for (const auto& s : signals){
                r += "            this->" + s.name + " = std::move(other." + s.name + ");\n";
            }
            for (const auto& b : bases){
                r += "            " + b + "::operator=(std::move(other));\n";
            }
            r += "        }\n";
            r += "        return *this;\n";
            r += "    }\n\n";
        } else {
            // 普通类：生成深拷贝
            r += "    " + name + "(const " + name + "& other)";
            std::vector<std::string> initList;
            for (const auto& f : fields){
                if (f.isStatic) continue;
                initList.push_back(f.name + "(other." + f.name + ")");
            }
            for (const auto& s : signals){
                initList.push_back(s.name + "(other." + s.name + ")");
            }
            for (std::size_t i = 0; i < bases.size(); ++i){
                initList.push_back(bases[i] + "(other)");
            }
            if (!initList.empty()){
                r += "\n        : ";
                for (std::size_t i = 0; i < initList.size(); ++i){
                    if (i > 0) r += ",\n          ";
                    r += initList[i];
                }
                r += "\n    ";
            }
            std::set<std::string> subscribedSigs;
            for (const auto& m : methods){
                if (m.isCtor) continue;
                for (const auto& sn : m.subscribesToShort) subscribedSigs.insert(sn);
            }
            if (subscribedSigs.empty()){
                r += "{}\n\n";
            } else {
                r += "{\n";
                for (const auto& sn : subscribedSigs){
                    r += "        this->" + sn + "._handlers.clear();\n";
                }
                for (const auto& m : methods){
                    if (m.isCtor) continue;
                    for (std::size_t si = 0; si < m.subscribesToShort.size(); ++si){
                        const std::string& shortName = m.subscribesToShort[si];
                        const auto& params = (si < m.subscribesToParams.size()) ? m.subscribesToParams[si] : std::vector<Param>{};
                        std::string lambdaArgs, callArgs;
                        for (std::size_t k = 0; k < params.size(); ++k){
                            if (k > 0){ lambdaArgs += ", "; callArgs += ", "; }
                            lambdaArgs += mapTypeToC(params[k].type) + " " + params[k].name;
                            callArgs += params[k].name;
                        }
                        r += "        this->" + shortName + ".connect([this](" + lambdaArgs + ") {\n";
                        r += "            this->" + m.name + "(" + callArgs + ");\n";
                        r += "        });\n";
                    }
                }
                r += "    }\n\n";
            }
            
            bool hasConstField = false;
            for (const auto& f : fields){
                if (f.isConst && !f.isStatic){ hasConstField = true; break; }
            }
            if (hasConstField){
                r += "    " + name + "& operator=(const " + name + "&) = delete;\n\n";
            } else {
                r += "    " + name + "& operator=(const " + name + "& other) {\n";
                r += "        if (this != &other) {\n";
                for (const auto& f : fields){
                    if (f.isStatic) continue;
                    r += "            this->" + f.name + " = other." + f.name + ";\n";
                }
                for (std::size_t i = 0; i < bases.size(); ++i){
                    bool isIface = (i < baseIsInterface.size()) ? baseIsInterface[i] : false;
                    if (!isIface) r += "            " + bases[i] + "::operator=(other);\n";
                }
                for (const auto& sn : subscribedSigs){
                    r += "            this->" + sn + "._handlers.clear();\n";
                }
                for (const auto& m : methods){
                    if (m.isCtor) continue;
                    for (std::size_t si = 0; si < m.subscribesToShort.size(); ++si){
                        const std::string& shortName = m.subscribesToShort[si];
                        const auto& params = (si < m.subscribesToParams.size()) ? m.subscribesToParams[si] : std::vector<Param>{};
                        std::string lambdaArgs, callArgs;
                        for (std::size_t k = 0; k < params.size(); ++k){
                            if (k > 0){ lambdaArgs += ", "; callArgs += ", "; }
                            lambdaArgs += mapTypeToC(params[k].type) + " " + params[k].name;
                            callArgs += params[k].name;
                        }
                        r += "            this->" + shortName + ".connect([this](" + lambdaArgs + ") {\n";
                        r += "                this->" + m.name + "(" + callArgs + ");\n";
                        r += "            });\n";
                    }
                }
                r += "        }\n";
                r += "        return *this;\n";
                r += "    }\n\n";
            }
        }
        // ==========================================================

        // ★ 查找 _new_ 方法，生成转换构造函数
        for (const auto& m : methods) {
            if (m.name == "_new_" && m.params.size() == 1) {
                std::string pType = mapTypeToC(m.params[0].type);
                std::string pName = m.params[0].name;
                
                std::vector<std::string> ctorInitList;
                auto isBasic = [](const std::string& t) {
                    if (t == "int" || t == "string" || t == "bool" || t == "char" || t == "void" || t == "any" ||
                        t == "float" || t == "double" || 
                        t == "int8" || t == "int16" || t == "int32" || t == "int64" ||
                        t == "uint8" || t == "uint16" || t == "uint32" || t == "uint64" ||
                        t.rfind("List<", 0) == 0 || t.rfind("FinalList<", 0) == 0 ||
                        t.rfind("Dict<", 0) == 0 || t.rfind("FinalDict<", 0) == 0) return true;
                    if (t.find("(") != std::string::npos) return true;
                    return false;
                };
                for (const auto& f : fields) {
                    if (f.isStatic) continue;
                    if (!isBasic(f.typeName)) {
                        ctorInitList.push_back(f.name + "(vox::NoInit())");
                    }
                }
                for (const auto& b : bases) {
                    ctorInitList.push_back(b + "(vox::NoInit())");
                }
                
                r += "    " + name + "(const " + pType + "& " + pName + ")";
                if (!ctorInitList.empty()) {
                    r += "\n        : ";
                    for (std::size_t i = 0; i < ctorInitList.size(); ++i) {
                        if (i > 0) r += ",\n          ";
                        r += ctorInitList[i];
                    }
                    r += "\n    ";
                }
                r += "{\n";
                r += "        _new_(" + pName + ");\n";
                r += "    }\n\n";
            }
        }

        // ★ 默认构造（用户没定义"能被无参调用"的 _init_ 时才生成）
        bool hasDefaultCtor = false;
        for (const auto& m : methods){
            if (!m.isCtor) continue;
            bool allHaveDefaults = true;
            for (const auto& p : m.params){
                if (!p.defaultValue){ allHaveDefaults = false; break; }
            }
            if (m.params.empty() || allHaveDefaults){
                hasDefaultCtor = true;
                break;
            }
        }
        if (!hasDefaultCtor){
            if (anyFieldConstraint){
                r += "    " + name + "() { _vox_check_fields_(); }\n\n";
            } else {
                r += "    " + name + "() = default;\n\n";
            }
        }

        // ★ 无副作用构造（专用于全局变量声明，避免触发用户 _init_）
        auto isBasicType = [](const std::string& t) {
            if (t == "int" || t == "string" || t == "bool" || t == "char" || t == "void" || t == "any" ||
                t == "float" || t == "double" ||
                t == "int8" || t == "int16" || t == "int32" || t == "int64" ||
                t == "uint8" || t == "uint16" || t == "uint32" || t == "uint64" ||
                t.rfind("List<", 0) == 0 || t.rfind("FinalList<", 0) == 0 ||
                t.rfind("Dict<", 0) == 0 || t.rfind("FinalDict<", 0) == 0) return true;
            if (t.find("(") != std::string::npos) return true;
            return false;
        };
        
        std::vector<std::string> noInits;
        for (const auto& f : fields) {
            if (f.isStatic) continue;
            if (!isBasicType(f.typeName)) {
                noInits.push_back(f.name + "(vox::NoInit())");
            }
        }
        for (const auto& s : signals) {
            noInits.push_back(s.name + "(vox::NoInit())");
        }
        for (const auto& b : bases) {
            noInits.push_back(b + "(vox::NoInit())");
        }
        
        r += "    " + name + "(vox::NoInit)";
        if (!noInits.empty()) {
            r += "\n        : ";
            for (std::size_t i = 0; i < noInits.size(); ++i) {
                if (i > 0) r += ",\n          ";
                r += noInits[i];
            }
            r += "\n    ";
        }
        r += " {}\n\n";

        // ========== struct：自动生成构造 / 运算符 / toString ==========
        if (isStruct){
            bool hasCtor = false;
            for (const auto& m : methods){
                if (m.isCtor){ hasCtor = true; break; }
            }

            if (!hasCtor){
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
                    r += "\n    {";
                    if (anyFieldConstraint) r += " _vox_check_fields_(); ";
                    r += "}\n\n";
                }
            }

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

                r += "    ";
                if (f.isStatic && f.isConst){
                    r += "static constexpr ";
                } else if (f.isStatic){
                    r += "static inline ";
                } else if (f.isConst){
                    r += "const ";
                }
                r += mapTypeToC(f.typeName) + " " + f.name;
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

                if (m.isCtor){
                    if (hasVariadic){
                        r += "    template<typename... __VoxVariadic>\n";
                        r += "    " + name + "(";
                    } else {
                        r += "    " + name + "(";
                    }
                    {
                        bool first = true;
                        for (const auto& p : m.params){
                            if (p.isVariadic) continue;
                            if (!first) r += ", ";
                            first = false;
                            if (p.isRef) r += mapTypeToC(p.type) + "& ";
                            else         r += mapTypeToC(p.type) + " ";
                            r += p.name;
                        }
                        for (const auto& p : m.params){
                            if (!p.isVariadic) continue;
                            if (!first) r += ", ";
                            first = false;
                            r += "__VoxVariadic... __vox_var_" + p.name;
                        }
                    }
                    r += ")";

                    std::vector<std::string> ctorInitList;
                    auto isBasic = [](const std::string& t) {
                        if (t == "int" || t == "string" || t == "bool" || t == "char" || t == "void" || t == "any" ||
                            t == "float" || t == "double" || 
                            t == "int8" || t == "int16" || t == "int32" || t == "int64" ||
                            t == "uint8" || t == "uint16" || t == "uint32" || t == "uint64" ||
                            t.rfind("List<", 0) == 0 || t.rfind("FinalList<", 0) == 0 ||
                            t.rfind("Dict<", 0) == 0 || t.rfind("FinalDict<", 0) == 0) return true;
                        if (t.find("(") != std::string::npos) return true;
                        return false;
                    };
                    for (const auto& f : fields) {
                        if (f.isStatic) continue;
                        if (!isBasic(f.typeName)) {
                            ctorInitList.push_back(f.name + "(vox::NoInit())");
                        }
                    }
                    for (const auto& b : bases) {
                        ctorInitList.push_back(b + "(vox::NoInit())");
                    }
                    
                    if (!ctorInitList.empty()) {
                        r += "\n        : ";
                        for (std::size_t i = 0; i < ctorInitList.size(); ++i) {
                            if (i > 0) r += ",\n          ";
                            r += ctorInitList[i];
                        }
                        r += "\n    ";
                    } else {
                        r += " ";
                    }
                    r += "{\n";

                    for (const auto& p : m.params){
                        if (p.isVariadic){
                            r += "        vox::List<" + mapTypeToC(p.type) + "> " + p.name + ";\n";
                            if (p.type == "string"){
                                r += "        (" + p.name + ".add(vox::vox_str(__vox_var_" + p.name + ")), ...);\n";
                            } else {
                                r += "        (" + p.name + ".add(__vox_var_" + p.name + "), ...);\n";
                            }
                        }
                    }
                    if (anyFieldConstraint){
                        r += "        _vox_check_fields_();\n";
                    }

                    int ctorCount = 0;
                    for (const auto& mm : methods){
                        if (mm.isCtor) ctorCount++;
                    }

                    if (ctorCount == 1){
                        r += "        _init_(";
                        {
                            bool firstArg = true;
                            for (const auto& p : m.params){
                                if (!firstArg) r += ", ";
                                firstArg = false;
                                if (p.isVariadic){
                                    r += "__vox_var_" + p.name + "...";
                                } else {
                                    r += p.name;
                                }
                            }
                        }
                        r += ");\n";
                    } else {
                        r += m.body->toC();
                    }
                    
                    for (const auto& m2 : methods){
                        if (m2.isCtor) continue;
                        for (std::size_t si = 0; si < m2.subscribesToShort.size(); ++si){
                            const std::string& shortName = m2.subscribesToShort[si];
                            const auto& params = (si < m2.subscribesToParams.size()) ? m2.subscribesToParams[si] : std::vector<Param>{};
                            std::string lambdaArgs, callArgs;
                            for (std::size_t k = 0; k < params.size(); ++k){
                                if (k > 0){ lambdaArgs += ", "; callArgs += ", "; }
                                lambdaArgs += mapTypeToC(params[k].type) + " " + params[k].name;
                                callArgs += params[k].name;
                            }
                            r += "        this->" + shortName + ".connect([this](" + lambdaArgs + ") {\n";
                            r += "            this->" + m2.name + "(" + callArgs + ");\n";
                            r += "        });\n";
                        }
                    }
                    r += "    }\n";

                    if (hasVariadic){
                        r += "    template<typename... __VoxVariadic>\n";
                        r += "    void _init_(";
                    } else {
                        r += "    virtual void _init_(";
                    }
                    {
                        bool first = true;
                        for (const auto& p : m.params){
                            if (p.isVariadic) continue;
                            if (!first) r += ", ";
                            first = false;
                            if (p.isRef) r += mapTypeToC(p.type) + "& ";
                            else         r += mapTypeToC(p.type) + " ";
                            r += p.name;
                        }
                        for (const auto& p : m.params){
                            if (!p.isVariadic) continue;
                            if (!first) r += ", ";
                            first = false;
                            r += "__VoxVariadic... __vox_var_" + p.name;
                        }
                    }
                    r += ") {\n";
                    for (const auto& p : m.params){
                        if (p.isVariadic){
                            r += "        vox::List<" + mapTypeToC(p.type) + "> " + p.name + ";\n";
                            if (p.type == "string"){
                                r += "        (" + p.name + ".add(vox::vox_str(__vox_var_" + p.name + ")), ...);\n";
                            } else {
                                r += "        (" + p.name + ".add(__vox_var_" + p.name + "), ...);\n";
                            }
                        }
                    }
                    for (const auto& p : m.params){
                        if (!p.isVariadic && p.constraint.has_value()){
                            r += "        " + p.constraint->toCCheck(p.name) + "\n";
                        }
                    }
                    r += m.body->toC();
                    r += "    }\n";
                    continue;
                }

                std::string prefix;
                if (m.isStatic)          prefix = "static ";
                else if (m.isDtor)       prefix = "";
                else                     prefix = "virtual ";
                std::string rt = m.returnType.empty() ? "void" : mapTypeToC(m.returnType);

                if (hasVariadic){
                    r += "    template<typename... __VoxVariadic>\n";
                    std::string prefix2 = prefix;
                    if (prefix2 == "virtual ") prefix2 = "";
                    r += "    " + prefix2 + rt + " " + m.name + "(";
                    bool first = true;
                    for (const auto& p : m.params){
                        if (p.isVariadic) continue;
                        if (!first) r += ", ";
                        first = false;
                        if (p.isRef) r += mapTypeToC(p.type) + "& ";
                        else         r += mapTypeToC(p.type) + " ";
                        r += p.name;
                        if (p.defaultValue) r += " = " + p.defaultValue->toC();
                    }
                    for (const auto& p : m.params){
                        if (!p.isVariadic) continue;
                        if (!first) r += ", ";
                        first = false;
                        r += "__VoxVariadic... __vox_var_" + p.name;
                    }
                    r += ") {\n";
                    for (const auto& p : m.params){
                        if (p.isVariadic){
                            r += "        vox::List<" + mapTypeToC(p.type) + "> " + p.name + ";\n";
                            if (p.type == "string"){
                                r += "        (" + p.name + ".add(vox::vox_str(__vox_var_" + p.name + ")), ...);\n";
                            } else {
                                r += "        (" + p.name + ".add(__vox_var_" + p.name + "), ...);\n";
                            }
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
                    r += "    " + prefix + rt + " " + m.name + "(";
                    for (std::size_t i = 0; i < m.params.size(); ++i){
                        if (i > 0) r += ", ";
                        if (m.params[i].isConst) r += "const ";
                        if (m.params[i].isRef) r += mapTypeToC(m.params[i].type) + "& ";
                        else                    r += mapTypeToC(m.params[i].type) + " ";
                        r += m.params[i].name;
                        if (m.params[i].defaultValue){
                            if (std::dynamic_pointer_cast<NullNode>(m.params[i].defaultValue)){
                                r += " = " + nullAsType(m.params[i].type);
                            } else {
                                r += " = " + m.params[i].defaultValue->toC();
                            }
                        }
                    }
                    r += ")";

                    if (m.isAbstract){
                        if (m.isConst) r += " const";
                        r += " = 0;\n";
                        continue;
                    }

                    if (m.isOverride && !bases.empty()) r += " override";
                    if (m.isConst) r += " const";
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

        auto emitSignals = [&](const std::string& access){
            for (const auto& s : signals){
                if (s.access != access) continue;
                std::string sigType = "vox::Signal<";
                for (std::size_t i = 0; i < s.params.size(); ++i){
                    if (i > 0) sigType += ", ";
                    sigType += mapTypeToC(s.params[i].type);
                }
                sigType += ">";
                r += "    " + sigType + " " + s.name + ";\n";
            }
        };

        r += "public:\n";
        emitFields("public"); emitSignals("public"); emitMethods("public");
        r += "protected:\n";
        emitFields("protected"); emitSignals("protected"); emitMethods("protected");
        r += "private:\n";
        emitFields("private"); emitSignals("private"); emitMethods("private");

        // ★ 有 _bool_ 魔法方法时，生成 operator bool（供 if / && / check 等使用）
        for (const auto& m : methods){
            if (m.name == "_bool_" && !m.isStatic && !m.isCtor && m.params.empty()){
                r += "public:\n";
                r += "    operator bool() { return _bool_(); }\n\n";
                break;
            }
        }

        // ★ 生成字段约束 helper
        if (anyFieldConstraint){
            r += "private:\n";
            r += "    void _vox_check_fields_() {\n";
            for (const auto& f : fields){
                if (f.constraint.has_value()){
                    r += "        " + f.constraint->toCCheck(f.name) + "\n";
                }
            }
            r += "    }\n";
        }

        // ★★★ 核心：输出 friend 声明 ★★★
        for (const auto& fd : friendDecls) {
            r += "    friend " + fd + ";\n";
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
        if (isAppend) return "";   // ★ append 不生成 class Player;
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
    std::string sourceFile;   // ★
    std::string sourceText;
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

        // ★ 顶层 class / enum 前向声明
        for (const auto& stmt : statements){
            if (auto cls = std::dynamic_pointer_cast<ClassNode>(stmt)){
                result += cls->forwardDecl() + "\n";
            }
            else if (auto en = std::dynamic_pointer_cast<EnumNode>(stmt)){
                result += en->forwardDecl() + "\n";
            }
        }

        // ★ namespace 内的 class 前向声明（含嵌套 namespace）
                std::function<void(const std::shared_ptr<NamespaceNode>&, const std::string&)> emitNsForward =
            [&](const std::shared_ptr<NamespaceNode>& ns, const std::string& indent){
                std::string inner;
                std::set<std::string> seen;
                for (const auto& s : ns->statements){
                    if (auto cls = std::dynamic_pointer_cast<ClassNode>(s)){
                        std::string decl = cls->forwardDecl();
                        if (!decl.empty() && seen.insert(cls->name).second){
                            inner += indent + "    " + decl + "\n";
                        }
                    }
                    else if (auto fn = std::dynamic_pointer_cast<FunctionDeclNode>(s)){
                        if (fn->isExtern) continue;

                        // ★ 只前向声明"签名全是基础类型/容器"的函数
                        auto isSimpleType = [](const std::string& t) {
                            if (t == "void" || t == "bool" || t == "char" ||
                                t == "int"  || t == "float" || t == "double" ||
                                t == "string" || t == "any" ||
                                t == "int8" || t == "int16" ||
                                t == "int32" || t == "int64" ||
                                t == "uint8" || t == "uint16" ||
                                t == "uint32" || t == "uint64")
                                return true;
                            if (t.compare(0, 5, "List<") == 0) return true;
                            if (t.compare(0, 5, "Dict<") == 0) return true;
                            if (t.compare(0, 10, "FinalList<") == 0) return true;
                            if (t.compare(0, 10, "FinalDict<") == 0) return true;
                            // ★ 允许已知类名 / 泛型类
                            if (!t.empty() && std::isupper(static_cast<unsigned char>(t[0]))) return true;
                            return false;
                        };
                        if (!isSimpleType(fn->returnType)) continue;
                        bool allSimple = true;
                        for (const auto& p : fn->params){
                            if (!isSimpleType(p.type)){ allSimple = false; break; }
                        }
                        if (!allSimple) continue;

                        std::string sig;
                        if (!fn->templateParams.empty()){
                            sig += "template <";
                            for (std::size_t i = 0; i < fn->templateParams.size(); ++i){
                                if (i > 0) sig += ", ";
                                sig += "typename " + fn->templateParams[i];
                            }
                            sig += ">\n" + indent + "    ";
                        }
                        sig += mapTypeToC(fn->returnType) + " " + fn->name + "(";
                        for (std::size_t i = 0; i < fn->params.size(); ++i){
                            if (i > 0) sig += ", ";
                            const Param& p = fn->params[i];
                            if (p.isConst) sig += "const ";
                            if (p.isRef)   sig += mapTypeToC(p.type) + "& ";
                            else           sig += mapTypeToC(p.type) + " ";
                            sig += p.name;
                        }
                        sig += ");";
                        if (seen.insert("fn:" + fn->name).second){
                            inner += indent + "    " + sig + "\n";
                        }
                    }
                    else if (auto en = std::dynamic_pointer_cast<EnumNode>(s)){
                        std::string decl = en->forwardDecl();
                        if (!decl.empty() && seen.insert(en->name).second){
                            inner += indent + "    " + decl + "\n";
                        }
                    }
                    else if (auto sub = std::dynamic_pointer_cast<NamespaceNode>(s)){
                        inner += indent + "    namespace " + sub->name + " {\n";
                        std::set<std::string> subSeen;
                        for (const auto& ss : sub->statements){
                            if (auto scls = std::dynamic_pointer_cast<ClassNode>(ss)){
                                std::string decl = scls->forwardDecl();
                                if (!decl.empty() && subSeen.insert(scls->name).second){
                                    inner += indent + "        " + decl + "\n";
                                }
                            }
                        }
                        inner += indent + "    }\n";
                    }
                }
                if (!inner.empty()){
                    result += indent + "namespace " + ns->name + " {\n" + inner + indent + "}\n";
                }
            };

            
        for (const auto& stmt : statements){
            if (auto ns = std::dynamic_pointer_cast<NamespaceNode>(stmt)){
                emitNsForward(ns, "");
            }
        }
        result += "\n";

        // ★ 收集"有 const 字段"的类名
        std::set<std::string> classesWithConstField;
        for (const auto& stmt : statements){
            if (auto cls = std::dynamic_pointer_cast<ClassNode>(stmt)){
                for (const auto& f : cls->fields){
                    if (f.isConst && !f.isStatic){
                        classesWithConstField.insert(cls->name);
                        break;
                    }
                }
            }
        }
        
        // ★ 全局变量 extern 声明 —— 放到 class 前，让类方法能引用
        for (const auto& stmt : statements){
            if (auto var = std::dynamic_pointer_cast<VariableDeclNode>(stmt)){
                if (var->isGlobal){
                    // ★ any 映射成 vox::Any（不是 auto），可以正常前向声明
                    std::string baseType;
                    if (var->typeName == "any"){
                        baseType = "vox::Any";
                    } else {
                        baseType = mapTypeToC(var->typeName);
                    }
                    for (const auto& n : var->names){
                        if (var->isConst){
                            result += "extern const " + baseType + " vox_g_" + n + ";\n";
                        } else {
                            result += "extern " + baseType + " vox_g_" + n + ";\n";
                        }
                    }
                }
            }
        }
        result += "\n";

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

                    // ★ any → vox::Any
                    //   非 const：声明处空 Any，延迟到 main 里赋值
                    //              （避免全局初始化阶段调用还没赋值的其他全局变量）
                    //   const：声明处初始化（const 不能后续赋值）
                    if (var->typeName == "any"){
                        for (std::size_t i = 0; i < var->names.size(); ++i){
                            std::string cppName = "vox_g_" + var->names[i];
                            if (var->isConst
                                && i < var->values.size()
                                && var->values[i]){
                                result += "inline const vox::Any " + cppName
                                        + "{" + var->values[i]->toC() + "};\n";
                            } else {
                                result += "inline vox::Any " + cppName + "{};\n";
                            }
                        }
                        continue;
                    }

                    // ★ 如果是 T? 类型，直接用默认初始化 {}
                    if (!var->typeName.empty() && var->typeName.back() == '?') {
                        for (std::size_t i = 0; i < var->names.size(); ++i){
                            std::string cppName = "vox_g_" + var->names[i];
                            result += "inline " + baseType + " " + cppName + "{};\n";
                        }
                        continue;
                    }

                    if (var->isConst){
                        for (std::size_t i = 0; i < var->names.size(); ++i){
                            std::string cppName = "vox_g_" + var->names[i];   // ★
                            result += "inline const " + baseType + " " + cppName;
                            if (i < var->values.size() && var->values[i]){
                                result += " = " + var->values[i]->toC();
                            }
                            result += ";\n";
                        }
                    } else {
                        for (std::size_t i = 0; i < var->names.size(); ++i){
                            std::string cppName = "vox_g_" + var->names[i];   // ★
                            if (isBuiltinCType(baseType)){
                                result += "inline " + baseType + " " + cppName + "{};\n";
                            } else {
                                std::string clsName = var->typeName;
                                auto pos = clsName.rfind("::");
                                if (pos != std::string::npos){
                                    clsName = clsName.substr(pos + 2);
                                }
                                if (classesWithConstField.count(clsName) > 0){
                                    if (i < var->values.size() && var->values[i]){
                                        result += "inline " + baseType + " " + cppName
                                                + "{" + var->values[i]->toC() + "};\n";
                                    } else {
                                        result += "inline " + baseType + " " + cppName + "{};\n";
                                    }
                                } else {
                                    result += "inline " + baseType + " " + cppName
                                            + "{vox::NoInit()};\n";
                                }
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
                        if (p.isConst) result += "const ";
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
        result += "int main(int argc, char** argv) {\n";
        result += "    vox::vox_init_console();\n";
        result += "    vox_set_main_source(\""
                + escapeCString(sourceFile) + "\");\n";
        result += "    vox_cli_init(argc, argv);\n";   // ★
        // ★ 顶层函数的信号订阅
        for (const auto& stmt : statements){
            auto fn = std::dynamic_pointer_cast<FunctionDeclNode>(stmt);
            if (!fn) continue;
            if (fn->isExtern) continue;
            if (fn->subscribesTo.empty()) continue;

            for (const auto& sigName : fn->subscribesTo){
                // 判断是实例信号还是全局信号
                bool isInstance = false;
                std::string target = sigName;
                auto dotPos = sigName.find('.');
                if (dotPos != std::string::npos){
                    // "character.e" → "vox_g_character.e"
                    std::string var = sigName.substr(0, dotPos);
                    std::string member = sigName.substr(dotPos + 1);
                    // 如果 var 是 namespace 别名，就不是实例
                    // 简化处理：名字不以大写开头 且 不是 namespace 别名 → 实例
                    // 更简单：先试 namespace 别名展开
                    // 这里简化为：只要不是已注册的全局信号短名，就当实例处理
                    // （因为顶层 `signal e()` 声明的短名后面会被用）
                    target = "vox_g_" + var + "." + member;
                    isInstance = true;
                }

                if (!isInstance){
                    // 全局信号：直接用名字（同 namespace 或 alias 展开）
                    // 简单情况下，短名直接可用（顶层声明的 signal 或 namespace 里的）
                    target = sigName;
                }

                std::string lambdaArgs, callArgs;
                for (std::size_t k = 0; k < fn->params.size(); ++k){
                    if (k > 0){ lambdaArgs += ", "; callArgs += ", "; }
                    lambdaArgs += mapTypeToC(fn->params[k].type) + " " + fn->params[k].name;
                    callArgs += fn->params[k].name;
                }

                result += "        " + target + ".connect([](" + lambdaArgs + ") {\n";
                result += "            " + fn->name + "(" + callArgs + ");\n";
                result += "        });\n";
            }
        }
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
            if (std::dynamic_pointer_cast<SignalDeclNode>(stmt)) continue;   // ★

            // 全局变量：在 main 里手动生成"赋值"
            if (auto var = std::dynamic_pointer_cast<VariableDeclNode>(stmt)){
                if (var->isGlobal){
                    if (var->isConst) continue;   // ★ const 已在顶层初始化，跳过
                    // ★ any 类型：main 里赋值
                    //   注意：const any 已经在声明处初始化，上面 isConst 已跳过
                    if (var->typeName == "any"){
                        for (std::size_t i = 0; i < var->names.size(); ++i){
                            std::string cppName = "vox_g_" + var->names[i];
                            result += "        " + cppName;
                            if (i < var->values.size() && var->values[i]){
                                result += " = " + var->values[i]->toC();
                            }
                            result += ";\n";
                        }
                        continue;
                    }
                    // ★ 只有"含 const 字段的类"跳过赋值（声明处已初始化）
                    std::string baseType = mapTypeToC(var->typeName);
                    if (!isBuiltinCType(baseType)){
                        std::string clsName = var->typeName;
                        auto pos = clsName.rfind("::");
                        if (pos != std::string::npos) clsName = clsName.substr(pos + 2);

                        if (classesWithConstField.count(clsName) > 0){
                            continue;   // 已直接初始化，跳过
                        }
                        // 其他：保持原逻辑，在 main 里赋值
                    }
                    // builtin：在 main 里赋值
                    for (std::size_t i = 0; i < var->names.size(); ++i){
                        std::string cppName = "vox_g_" + var->names[i];   // ★
                        result += "        " + cppName;
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

            // ★ 顶层代码在生成的 int main 里，裸 "return;" 必须变成 "return 0;"
            //   只对"完整 token 边界"的 return; 做替换，避免破坏 "returned;" 之类
            {
                std::size_t p = 0;
                while ((p = code.find("return;", p)) != std::string::npos){
                    bool leftOK = (p == 0)
                        || !(std::isalnum(static_cast<unsigned char>(code[p - 1]))
                            || code[p - 1] == '_');
                    if (leftOK){
                        code.replace(p, 7, "return 0;");
                        p += 9;
                    } else {
                        p += 7;
                    }
                }
            }

            // ★ 加 FrameGuard —— 让异常能回溯到这条语句
            int ln = 0;
            if (auto a = std::dynamic_pointer_cast<AssignmentNode>(stmt))    ln = a->line;
            else if (auto v = std::dynamic_pointer_cast<VariableDeclNode>(stmt)) ln = v->line;
            else if (auto e = std::dynamic_pointer_cast<ExpressionStatementNode>(stmt)){
                if (e->expr){
                    if (auto c = std::dynamic_pointer_cast<CallNode>(e->expr)) ln = c->line;
                }
            }
            else if (auto r = std::dynamic_pointer_cast<ReturnNode>(stmt))   ln = r->line;
            else if (auto b = std::dynamic_pointer_cast<BreakNode>(stmt))    ln = b->line;
            else if (auto cc = std::dynamic_pointer_cast<ContinueNode>(stmt))ln = cc->line;

            // ★ ExpressionStatementNode 自己已经带 FrameGuard，避免重复
            bool alreadyHasGuard = false;
            if (auto es = std::dynamic_pointer_cast<ExpressionStatementNode>(stmt)){
                if (es->hasTrace) alreadyHasGuard = true;
            }

            if (ln > 0 && !sourceText.empty() && !alreadyHasGuard){
                // 读源码行...
                std::string srcLine;
                {
                    std::size_t start = 0;
                    int cl = 1;
                    while (cl < ln){
                        auto pos = sourceText.find('\n', start);
                        if (pos == std::string::npos){ start = sourceText.size(); break; }
                        start = pos + 1;
                        ++cl;
                    }
                    auto end = sourceText.find('\n', start);
                    if (end == std::string::npos) end = sourceText.size();
                    while (end > start && sourceText[end - 1] == '\r') end--;
                    srcLine = sourceText.substr(start, end - start);
                }
                int colStart = 0;
                while (colStart < (int)srcLine.size() && srcLine[colStart] == ' ') colStart++;
                int colLen = (int)srcLine.size() - colStart;

                result += "        { vox::FrameGuard _vfg(\""
                    + escapeCString(sourceFile) + "\", "
                    + std::to_string(ln) + ", \""
                    + escapeCString(srcLine) + "\", "
                    + std::to_string(colStart) + ", "
                    + std::to_string(colLen) + ", -1, 0, __func__); "
                    + code + " }\n";
            } else {
                result += "        " + code + "\n";
            }
        }
        result += "    } catch (const vox::Exception& e) {\n";
        result += "        std::cout << e.exception() << std::endl;\n";
        result += "        return 1;\n";
        result += "    } catch (const std::exception& e) {\n";      // ★
        result += "        std::cerr << \"std::exception: \" << e.what() << std::endl;\n";
        result += "        return 2;\n";
        result += "    }\n";
        result += "    return 0;\n";
        result += "}\n";

        return result;
    }
};

#endif