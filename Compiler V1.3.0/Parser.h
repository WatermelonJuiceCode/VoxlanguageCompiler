#ifndef PARSER_H
#define PARSER_H

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <map>
#include <functional>

#include "lexer.h"
#include "token.h"
#include "exception.h"
#include "variables.h"
#include "AstNodes.h"

class Parser{
public:
    Parser(std::vector<Token> tokens, std::string text, std::string fileName,
        std::string baseDir = "",
        std::set<std::string>* importedModules = nullptr,
        std::vector<std::string> searchDirs = {})
        : _tokens(std::move(tokens)),
        _fileName(std::move(fileName)),
        _text(std::move(text)),
        _baseDir(std::move(baseDir)),
        _searchDirs(std::move(searchDirs)),
        _pos(0) {
        // ★ 顶层 Parser：调用方不传 importedModules；子 Parser 传父的指针
        _isTopLevel = (importedModules == nullptr);
        if (importedModules) _importedModules = *importedModules;
        for (const auto& e : exceptions) _knownExceptions.insert(e);
    }
    
    // 预扫描：把当前文件里所有 class 名收进 _knownClasses，
    // 让"类 A 里引用后面才声明的类 B"也能识别为静态访问。
    void prescanClassNames(){
        int savedPos = _pos;
        _pos = 0;
        while (!check(TokenType::END)){
            if (check(TokenType::KwClass) || check(TokenType::KwInterface)){
                bool isIface = check(TokenType::KwInterface);
                advance();
                if (check(TokenType::Identifier)){
                    if (isIface) {
                        _knownInterfaces.insert(current().value);
                    } else {
                        _knownClasses.insert(current().value);
                    }
                    advance();
                }
            } else {
                advance();
            }
        }
        _pos = savedPos;
    }

    AstNodePtr parse(){
        auto program = std::make_shared<ProgramNode>();
        _topLevelProgram = program;

        program->sourceFile = _fileName;
        program->sourceText = _text;

        _constraintStack.clear();
        _constraintStack.push_back({});   // ★ 顶层约束作用域

        prescanClassNames();      

        while (!check(TokenType::END)){
            if (check(TokenType::At)){
                auto attrs = parseAttributes();
                if (check(TokenType::KwClass)){
                    auto cls = std::dynamic_pointer_cast<ClassNode>(parseClass());
                    cls->attributes = std::move(attrs);
                    program->add(cls);
                } else if (check(TokenType::KwStruct)){
                    auto cls = std::dynamic_pointer_cast<ClassNode>(parseClass());
                    cls->attributes = std::move(attrs);
                    cls->isStruct = true;
                    program->add(cls);
                } else if (startsDeclaration()){
                    AstNodePtr decl = parseDeclaration();
                    if (auto fn = std::dynamic_pointer_cast<FunctionDeclNode>(decl)){
                        fn->attributes = std::move(attrs);
                    }
                    if (auto var = std::dynamic_pointer_cast<VariableDeclNode>(decl)){
                        var->isGlobal = true;   // ← 新增
                    }
                    program->add(decl);
                    if (std::dynamic_pointer_cast<VariableDeclNode>(decl)){
                        program->add(std::make_shared<SemicolonNode>());
                    }
                } else if (check(TokenType::KwInterface)){
                    auto cls = std::dynamic_pointer_cast<ClassNode>(parseClass(true));
                    cls->attributes = std::move(attrs);
                    cls->isInterface = true;
                    program->add(cls);
                } else {
                    raise(EX_SYNTAX,
                        "Expected declaration after attributes",
                        current().line);
                }
            } else if (check(TokenType::KwTemplate)){
                auto tmplParams = parseTemplatePrefix();

                if (check(TokenType::KwClass)){
                    auto cls = std::dynamic_pointer_cast<ClassNode>(parseClass());
                    cls->templateParams = std::move(tmplParams);
                    program->add(cls);
                } else if (check(TokenType::KwStruct)){
                    auto cls = std::dynamic_pointer_cast<ClassNode>(parseClass());
                    cls->isStruct = true;
                    cls->templateParams = std::move(tmplParams);
                    program->add(cls);
                } else if (startsDeclaration()){
                    AstNodePtr decl = parseDeclaration();
                    if (auto fn = std::dynamic_pointer_cast<FunctionDeclNode>(decl)){
                        fn->templateParams = std::move(tmplParams);
                    }
                    if (auto var = std::dynamic_pointer_cast<VariableDeclNode>(decl)){
                        var->isGlobal = true;
                    }
                    program->add(decl);
                    if (std::dynamic_pointer_cast<VariableDeclNode>(decl)){
                        program->add(std::make_shared<SemicolonNode>());
                    }
                } else {
                    raise(EX_SYNTAX,
                          "Expected class, struct, or function after 'template<...>'",
                          current().line);
                }
            } else if (check(TokenType::Using)){
                parseUsing(program);
            } else if (check(TokenType::KwRaise)){
                program->add(parseRaise());
            } else if (check(TokenType::KwAbstract)){
                int ln = current().line;
                advance();
                if (!check(TokenType::KwClass)){
                    raise(EX_SYNTAX,
                          "Expected 'class' after 'abstract'", ln);
                }
                auto cls = std::dynamic_pointer_cast<ClassNode>(parseClass());
                cls->isAbstract = true;
                program->add(cls);
            } else if (check(TokenType::KwClass)){
                auto cls = std::dynamic_pointer_cast<ClassNode>(parseClass());
                program->add(cls);
            } else if (check(TokenType::KwStruct)){
                auto cls = std::dynamic_pointer_cast<ClassNode>(parseClass());
                cls->isStruct = true;
                program->add(cls);
            } else if (check(TokenType::KwInterface)){
                auto cls = std::dynamic_pointer_cast<ClassNode>(parseClass(true));
                cls->isInterface = true;
                program->add(cls);
            } else if (check(TokenType::KwNamespace)){
                program->add(parseNamespace());
            } else if (check(TokenType::KwExtern)){
                program->add(parseExtern());
            } else if (check(TokenType::KwFor)){
                program->add(parseFor());
            } else if (check(TokenType::KwForeach)){
                program->add(parseForeach());
            } else if (check(TokenType::KwIf)){
                program->add(parseIf());
            } else if (check(TokenType::KwWhile)){
                program->add(parseWhile());
            } else if (check(TokenType::KwDo)){
                program->add(parseDoWhile());
            } else if (check(TokenType::KwTry)){
                program->add(parseTry());
            } else if (check(TokenType::KwSwitch)){
                program->add(parseSwitch());
            } else if (check(TokenType::KwWith)){          // ★
                program->add(parseWith());
            } else if (check(TokenType::KwBreak)){
                int ln = current().line;   // ★
                advance();
                eat(TokenType::Semicolon);
                program->add(std::make_shared<BreakNode>(ln));
            } else if (check(TokenType::KwContinue)){
                int ln = current().line;   // ★
                advance();
                eat(TokenType::Semicolon);
                program->add(std::make_shared<ContinueNode>(ln));
            } else if (check(TokenType::KwEnum)){
                program->add(parseEnum());
            } else if (startsDeclaration()){
                AstNodePtr decl = parseDeclaration();
                if (auto var = std::dynamic_pointer_cast<VariableDeclNode>(decl)){
                    var->isGlobal = true;
                }
                program->add(decl);
                if (std::dynamic_pointer_cast<VariableDeclNode>(decl)){
                    program->add(std::make_shared<SemicolonNode>());
                }
            } else if (check(TokenType::Identifier)){
                program->add(parseExpressionStatement());
            } else {
                raise(EX_SYNTAX,
                    "Expected declaration, assignment, expression, class, or 'using', "
                    "but encountered " + tokenTypeName(current().type),
                    current().line);
            }
        }

        program->add(std::make_shared<EndNode>());

        // ==================== 签名收集 ====================
        // collectSignatures 里有 NamespaceNode 分支，会自动递归进 namespace，
        // 不需要 flatten，也不能跑两遍（会导致 variadic 参数被重排两次）
        collectNewSignatures(program);
        for (const auto& stmt : program->statements){
            resolveCalls(stmt);
        }

        // ---- 轻量级类型检查 ----
        _typeScopeStack.clear();
        _typeScopeStack.push_back({});
        _constScopeStack.clear();
        _constScopeStack.push_back({});

        // ★ 预填充顶层变量名（允许函数体前向引用全局变量）
        for (const auto& stmt : program->statements){
            if (auto var = std::dynamic_pointer_cast<VariableDeclNode>(stmt)){
                for (const auto& n : var->names){
                    _typeScopeStack[0][n] = var->typeName;
                }
            }
        }

        // ★ 收集类成员 + 命名空间成员
        for (const auto& stmt : program->statements){
            collectMembers(stmt, "");   // 顶层
        }

        _semLoopDepth = 0;
        _semSwitchDepth = 0;
        _semCurrentReturnType.clear();
        _nsDepth = 0;

        // ★ 收集每个类的 const 字段名（必须在 typeCheckStmt 之前）
        for (const auto& stmt : program->statements){
            if (auto cls = std::dynamic_pointer_cast<ClassNode>(stmt)){
                std::set<std::string> cf;
                for (const auto& f : cls->fields){
                    if (f.isConst && !f.isStatic){
                        cf.insert(f.name);
                    }
                }
                if (!cf.empty()){
                    _classConstFields[cls->name] = cf;
                }
            }
        }

        for (const auto& stmt : program->statements){
            typeCheckStmt(stmt);
        }

        // ★ 魔法方法签名校验
        for (const auto& stmt : program->statements){
            checkAllMagicMethods(stmt);
        }

        // ★ 不可达代码检查（警告）
        for (const auto& stmt : program->statements){
            checkUnreachable(stmt);
        }

        // ★ 未使用变量警告
        emitUnusedVarWarnings();

        // ★ 语义检查 1：所有非 void 的函数 / lambda / 方法必须返回值
        for (const auto& stmt : program->statements){
            checkReturnStatements(stmt);
        }

        // ★ 语义检查 2：顶层变量不能重名
        checkTopLevelVarDuplicates(program);

        // ★ 登记每个类的第一个非 interface 基类
        for (const auto& stmt : program->statements){
            if (auto cls = std::dynamic_pointer_cast<ClassNode>(stmt)){
                for (std::size_t i = 0; i < cls->bases.size(); ++i){
                    bool isIface = (i < cls->baseIsInterface.size())
                                   ? cls->baseIsInterface[i] : false;
                    if (!isIface){
                        _classBaseMap[cls->name] = cls->bases[i];
                        break;
                    }
                }
            }
        }

        // ★ 收集所有 interface 的方法集
        for (const auto& stmt : program->statements){
            if (auto cls = std::dynamic_pointer_cast<ClassNode>(stmt)){
                if (cls->isInterface){
                    std::set<std::string> ms;
                    for (const auto& m : cls->methods){
                        ms.insert(m.name);
                    }
                    _interfaceMethods[cls->name] = ms;
                }
            }
        }

        // ★ 收集所有 abstract 类的方法集
        for (const auto& stmt : program->statements){
            if (auto cls = std::dynamic_pointer_cast<ClassNode>(stmt)){
                std::set<std::string> ms;
                for (const auto& m : cls->methods){
                    if (m.isAbstract) ms.insert(m.name);
                }
                if (!ms.empty()){
                    _abstractClassMethods[cls->name] = ms;
                }
            }
        }

        // ★ 检查类是否实现了所有 interface + abstract 方法 + 标记 override
        for (const auto& stmt : program->statements){
            if (auto cls = std::dynamic_pointer_cast<ClassNode>(stmt)){
                if (cls->isInterface) { continue; }
                markAndCheckInterfaces(*cls);
                markAndCheckAbstractMethods(*cls);
            }
        }

        // ★ 顶层 Parser 必须引入了 Code 标准库，否则 vox_cli_init 链接会失败
        if (_isTopLevel
            && _importedModules.find("vox_mod_Code") == _importedModules.end()){
            // 用第一个非空行的行号（跳过纯注释/空行）
            int errLine = 1;
            for (const auto& t : _tokens){
                if (t.type == TokenType::END) break;
                if (t.line > 0){ errLine = t.line; break; }
            }
            raise(EX_SYNTAX,
                  "missing 'using Code;' — every Vox program must import "
                  "the standard library at the top of the file\n"
                  "  (hint: add 'using Code;' as the first line)",
                  errLine);
        }

        return program;
    }

    const std::vector<std::string>& warnings() const { return _warnings; }

private:
    std::vector<Token> _tokens;
    std::string _fileName, _text;
    std::string _baseDir;
    int _pos;
    Token _lastToken = Token(TokenType::END, "", 0);
    std::set<std::string> _knownNamespaces;
    std::set<std::string> _knownExceptions;
    std::set<std::string> _knownClasses;   // 所有已知类名，用于区分 Class.method / obj.method
    std::map<std::string, std::string> _classToNamespace;   // "XmlNode" → "vox_mod_Code_xml"
    std::map<std::string, std::set<std::string>> _classInstanceFields;
    std::set<std::string> _knownInterfaces;
    std::map<std::string, std::string> _knownEnumMembers;       // "Color.Red" → "Color"
    std::set<std::string> _knownEnums;                          // 所有已知枚举名
    std::set<std::string> _classesWithBoolOp;   // 有 _bool_ 的类名
    std::map<std::string, std::set<std::string>> _interfaceMethods;
    std::map<std::string, std::set<std::string>> _abstractClassMethods;
    std::map<std::string, std::set<std::string>> _classConstFields;   // ★ "Test" → {"x"}
    // ★ 类名 → 所有成员名（字段 + 方法）
    std::map<std::string, std::set<std::string>> _classMembers;
    // ★ 类名 → 实例字段名 → 类型（供 `self.field = null` / `self.field = cond ? null : x` 用）
    std::map<std::string, std::map<std::string, std::string>> _classFieldTypes;
    // ★ 命名空间名（如 vox_mod_Code_window）→ 所有顶层符号名
    std::map<std::string, std::set<std::string>> _namespaceMembers;
    // ★ 命名空间内变量名 → 类型（"vox_mod_Code_window" → {"draw": "Draw"})
    std::map<std::string, std::map<std::string, std::string>> _namespaceVars;
    std::map<std::string, std::vector<std::vector<Param>>> _functionSignatures;
    std::set<std::string> _genericFunctions;   // ★ 泛型自由函数短名
    std::vector<std::map<std::string, std::string>> _typeScopeStack;
    std::vector<std::set<std::string>> _constScopeStack;   // 每个作用域的 const 变量名
    std::set<std::string> _voxFunctionNames;   // Vox 定义的顶层自由函数
    std::map<std::string, std::string> _namespaceAliases;  // "math" -> "vox_mod_math"
    std::shared_ptr<ProgramNode> _topLevelProgram;
    std::set<std::string> _importedModules;
    std::set<std::string> _importAllNs;        // ★ 本 Parser 用 `.*` 导入的命名空间
    const std::set<std::string>& importedModules() const { return _importedModules; }
    std::vector<std::vector<std::string>> _scopeVarStack;
    std::set<std::string> _optionalLoopVars;   // ★ 多变量 foreach 的 optional 变量名
    std::vector<std::string> _searchDirs;
    std::set<std::string> _templateParams;   // 当前可见的模板参数名
    std::set<std::string> _globalVarNames;   // ★ 所有已知的跨模块顶层变量名
    std::vector<std::map<std::string, ConstraintSet>> _constraintStack;
    std::string _currentClassName;                          // 当前类的短名
    std::map<std::string, std::string> _classBaseMap;       // "Dog" → "vox_mod_Code_xxx::Animal"
    std::set<std::string> _classesWithWith;   // 有 _with_ 方法的类
    // ---- 语义警告 ----
    std::vector<std::string> _warnings;             // 每条已格式化好
    std::map<std::string, int> _varDeclLine;        // 变量名 → 声明行
    std::set<std::string> _varUsed;                 // 被引用过的变量名
    int _nsDepth = 0;                               // 当前嵌套在 namespace 里的深度
    int _semLoopDepth = 0;              // 循环深度
    int _semSwitchDepth = 0;            // switch 深度（break 合法）
    std::string _semCurrentReturnType;  // 当前函数返回类型
    std::size_t _collectedStmtCount = 0;
    bool _isTopLevel = false;   // ★ 顶层 Parser（非子模块）才检查 using Code;

    Token current() const { return _tokens[_pos]; }
    Token peek(std::size_t offset = 1) const {
        std::size_t idx = _pos + offset;
        if (idx >= _tokens.size()) return _tokens.back();
        return _tokens[idx];
    }
    Token advance(){
        Token token = current();
        if (token.type != TokenType::END){
            _lastToken = token;   // ★ 更新
            _pos++;
        }
        return token;
    }
    bool check(TokenType type) const { return current().type == type; }
    bool match(TokenType type){
        if (check(type)){ advance(); return true; }
        return false;
    }
    void eat(TokenType type){
        if (check(type)){ advance(); return; }
        raise(EX_SYNTAX, "Expected " + tokenTypeName(type) +
              ", but encountered " + tokenTypeName(current().type),
              current().line);
    }

    void eatGreater(){
        if (check(TokenType::Greater)){
            advance();
            return;
        }
        if (check(TokenType::Shr)){
            // 把 >> 拆成两个 >：当前位置改成 Greater，下一位插入一个 Greater
            _tokens[_pos].type  = TokenType::Greater;
            _tokens[_pos].value = ">";
            Token gt(TokenType::Greater, ">", _tokens[_pos].line);
            _tokens.insert(_tokens.begin() + _pos + 1, gt);
            advance();      // 消费第一个 >
            return;
        }
        raise(EX_SYNTAX,
            "Expected '>', but encountered " + tokenTypeName(current().type),
            current().line);
    }

    bool isTypeKeyword(TokenType type) const {
        return type == TokenType::KwInt ||
               type == TokenType::KwString ||
               type == TokenType::KwBool ||
               type == TokenType::KwChar ||
               type == TokenType::KwVoid ||
               type == TokenType::KwAny ||
               type == TokenType::KwInt8   || type == TokenType::KwInt16 ||
               type == TokenType::KwInt32  || type == TokenType::KwInt64 ||
               type == TokenType::KwUInt8  || type == TokenType::KwUInt16 ||
               type == TokenType::KwUInt32 || type == TokenType::KwUInt64 ||
               type == TokenType::KwFloat  || type == TokenType::KwDouble;
    }

    bool isContainerStart(TokenType type) const {
        return type == TokenType::KwList ||
               type == TokenType::KwFinalList ||
               type == TokenType::KwDict ||
               type == TokenType::KwFinalDict;
    }

    // 检查类实现了所有 interface 方法，并标记 override
    void markAndCheckInterfaces(ClassNode& cls){
        // 1. 收集类的所有方法名
        std::set<std::string> classMethods;
        for (const auto& m : cls.methods){
            classMethods.insert(m.name);
        }

        // 2. 遍历基类，找到所有 interface，检查方法
        for (const auto& base : cls.bases){
            auto it = _interfaceMethods.find(base);
            if (it == _interfaceMethods.end()) { continue; }

            const std::set<std::string>& ifaceMs = it->second;
            for (const auto& mname : ifaceMs){
                if (classMethods.count(mname) == 0){
                    raise(EX_TYPE,
                          "Class '" + cls.name + "' does not implement "
                          "interface method '" + base + "." + mname + "()'",
                          0);
                }
            }
        }

        // 3. 标记 override
        for (auto& m : cls.methods){
            for (const auto& base : cls.bases){
                auto it = _interfaceMethods.find(base);
                if (it == _interfaceMethods.end()) { continue; }
                if (it->second.count(m.name) > 0){
                    m.isOverride = true;
                    break;
                }
            }
        }
    }

    void markAndCheckAbstractMethods(ClassNode& cls){
        // 收集类的方法名
        std::set<std::string> classMethods;
        for (const auto& m : cls.methods){
            classMethods.insert(m.name);
        }

        // 遍历基类，找 abstract 类
        for (const auto& base : cls.bases){
            // base 可能是 "vox_mod_Code_x::Foo" 或 "Foo"，两种都试
            std::string shortBase = base;
            auto pos = shortBase.rfind("::");
            if (pos != std::string::npos) shortBase = shortBase.substr(pos + 2);

            auto it = _abstractClassMethods.find(base);
            if (it == _abstractClassMethods.end()){
                it = _abstractClassMethods.find(shortBase);
            }
            if (it == _abstractClassMethods.end()) continue;

            const std::set<std::string>& absMs = it->second;
            for (const auto& mname : absMs){
                if (classMethods.count(mname) == 0 && !cls.isAbstract){
                    raise(EX_TYPE,
                          "Class '" + cls.name + "' must implement abstract method '"
                          + shortBase + "." + mname + "()'",
                          0);
                }
            }
        }

        // 标记 override
        for (auto& m : cls.methods){
            for (const auto& base : cls.bases){
                std::string shortBase = base;
                auto pos = shortBase.rfind("::");
                if (pos != std::string::npos) shortBase = shortBase.substr(pos + 2);

                auto it = _abstractClassMethods.find(base);
                if (it == _abstractClassMethods.end()){
                    it = _abstractClassMethods.find(shortBase);
                }
                if (it == _abstractClassMethods.end()) continue;
                if (it->second.count(m.name) > 0){
                    m.isOverride = true;
                    break;
                }
            }
        }
    }

    bool startsDeclaration() const {
        if (check(TokenType::KwConst)) return true;
        if (isContainerStart(current().type)) return true;

        // 类型关键字开头：int x / int[] x
        if (isTypeKeyword(current().type)){
            // int x
            if (peek(1).type == TokenType::Identifier) return true;
            // int[] x
            if (peek(1).type == TokenType::LBracket &&
                peek(2).type == TokenType::RBracket &&
                peek(3).type == TokenType::Identifier) return true;
            // int(int) x —— 函数类型变量
            if (peek(1).type == TokenType::LParen){
                std::size_t i = 1;
                int depth = 0;
                while (_pos + i < _tokens.size()){
                    TokenType t = _tokens[_pos + i].type;
                    if (t == TokenType::END) return false;
                    if (t == TokenType::LParen) depth++;
                    else if (t == TokenType::RParen){
                        depth--;
                        if (depth == 0){ i++; break; }
                    }
                    i++;
                }
                if (_pos + i < _tokens.size() &&
                    _tokens[_pos + i].type == TokenType::Identifier){
                    return true;
                }
            }
            return false;
        }

        // Identifier 开头：可能是类型名（可能带 . 限定、可能带 <...> 泛型）
        if (check(TokenType::Identifier)){
            std::size_t i = 1;

            // 跳过 .Identifier 段（命名空间限定）
            while (_pos + i + 1 < _tokens.size() &&
                _tokens[_pos + i].type == TokenType::Dot &&
                _tokens[_pos + i + 1].type == TokenType::Identifier){
                i += 2;
            }

            // ★ 跳过 <...> 泛型参数 —— 这是关键新增
            if (_pos + i < _tokens.size() &&
                _tokens[_pos + i].type == TokenType::Less){
                int depth = 1;
                i++;
                while (_pos + i < _tokens.size() && depth > 0){
                    TokenType t = _tokens[_pos + i].type;
                    if (t == TokenType::Less) depth++;
                    else if (t == TokenType::Greater) depth--;
                    else if (t == TokenType::Shr) depth -= 2;
                    else if (t == TokenType::END) return false;
                    i++;
                }
                if (depth != 0) return false;
            }

            // 情况1：TypeName identifier
            if (_pos + i < _tokens.size() &&
                _tokens[_pos + i].type == TokenType::Identifier) return true;

            // 情况2：TypeName[] identifier
            if (_pos + i + 2 < _tokens.size() &&
                _tokens[_pos + i].type == TokenType::LBracket &&
                _tokens[_pos + i + 1].type == TokenType::RBracket &&
                _tokens[_pos + i + 2].type == TokenType::Identifier) return true;
        }

        return false;
    }

    // ★ 从 MemberAccessNode / VariableNode 提取 Vox 原样的路径文本
    static std::string pathTextOf(const AstNodePtr& node){
        if (auto v = std::dynamic_pointer_cast<VariableNode>(node)){
            return v->name;
        }
        if (auto m = std::dynamic_pointer_cast<MemberAccessNode>(node)){
            std::string base = pathTextOf(m->object);
            if (base.empty()) return "";
            return base + "." + m->member;
        }
        return "";
    }

    static std::string lastSegment(const std::string& name){
        auto dot = name.rfind('.');
        return (dot == std::string::npos) ? name : name.substr(dot + 1);
    }

    // ---------------- 路径解析 ----------------
    // 在多个搜索路径里找存在的文件——找不到返回 ""
    std::string findFile(const std::string& rel) const {
        namespace fs = std::filesystem;
        std::vector<std::string> bases;
        if (!_baseDir.empty()) bases.push_back(_baseDir);
        for (const auto& d : _searchDirs) bases.push_back(d);
        bases.push_back(".");   // 兜底

        for (const auto& base : bases){
            fs::path p = fs::path(base) / rel;
            std::error_code ec;
            if (fs::exists(p, ec) && fs::is_regular_file(p, ec)){
                return p.string();
            }
        }
        return "";
    }

    // 在多个搜索路径里找目录下的 _init_.v / _init_.voxs
    std::vector<std::string> findInitFiles(const std::string& relDir) const {
        namespace fs = std::filesystem;
        std::vector<std::string> bases;
        if (!_baseDir.empty()) bases.push_back(_baseDir);
        for (const auto& d : _searchDirs) bases.push_back(d);
        bases.push_back(".");

        for (const auto& base : bases){
            fs::path dirPath = fs::path(base) / relDir;
            std::error_code ec;
            if (!fs::is_directory(dirPath, ec)) continue;

            std::vector<std::string> result;
            for (const auto& initName : {"_init_.v", "_init_.voxs"}){
                fs::path initPath = dirPath / std::string(initName);
                if (fs::exists(initPath, ec) && fs::is_regular_file(initPath, ec)){
                    result.push_back(initPath.string());
                }
            }
            if (!result.empty()) return result;
        }
        return {};
    }

    // ---------------- 类型名 ----------------
    std::string parseTypeName(){
        std::string tn;

        if (isContainerStart(current().type)){
            tn = advance().value;
            if (match(TokenType::Less)){
                tn += "<" + parseTypeName();
                while (match(TokenType::Comma)){
                    tn += ", " + parseTypeName();
                }
                eatGreater();
                tn += ">";
            }
        } else if (isTypeKeyword(current().type)){
            tn = advance().value;
            // ★ 函数类型：ReturnType(ParamType1 [name1], ParamType2 [name2], ...)
            if (check(TokenType::LParen)){
                advance();   // (
                tn += "(";
                if (!check(TokenType::RParen)){
                    tn += parseTypeName();
                    if (check(TokenType::Identifier)){
                        advance();
                    }
                    while (match(TokenType::Comma)){
                        tn += ", " + parseTypeName();
                        if (check(TokenType::Identifier)){
                            advance();
                        }
                    }
                }
                eat(TokenType::RParen);
                tn += ")";
            }
            // ★ 泛型参数（用于 Foo<List<int>> 这种写法）
            if (check(TokenType::Less)){
                advance();
                tn += "<";
                tn += parseTypeName();
                while (match(TokenType::Comma)){
                    tn += ", " + parseTypeName();
                }
                eatGreater();
                tn += ">";
            }
        } else if (check(TokenType::Identifier)){
            std::string first = advance().value;

            // 展开命名空间别名
            auto it = _namespaceAliases.find(first);
            if (it != _namespaceAliases.end()){
                tn = it->second;
            } else {
                tn = first;
            }

            // ★ 保留"Vox 原样路径"用来查多级别名
            std::string rawPath = first;

            // 支持 a.b.c 形式
            while (check(TokenType::Dot) && peek(1).type == TokenType::Identifier){
                advance();  // 吃 .
                Token seg = advance();

                // ★ 先尝试多级别名：rawPath + "." + seg
                std::string candidate = rawPath + "." + seg.value;
                auto aliasIt = _namespaceAliases.find(candidate);
                if (aliasIt != _namespaceAliases.end()){
                    // 命中：整个替换成真实命名空间
                    tn = aliasIt->second;
                    rawPath = candidate;
                    continue;
                }

                // 否则按原逻辑拼
                tn += "::" + seg.value;
                rawPath = candidate;
            }

            // ★ 补充命名空间前缀：裸类名 → 全限定名
            //   例：File → vox_mod_Code::File
            //   只在当前 tn 没有 "::" 且类确实定义在某个命名空间中时才补
            if (tn.find("::") == std::string::npos){
                auto cit = _classToNamespace.find(tn);
                if (cit != _classToNamespace.end() && !cit->second.empty()){
                    tn = cit->second + "::" + tn;
                }
            }

            // ★ 泛型参数 <T, U, ...>
            if (check(TokenType::Less)){
                advance();      // <
                tn += "<";
                tn += parseTypeName();
                while (match(TokenType::Comma)){
                    tn += ", " + parseTypeName();
                }
                eatGreater();
                tn += ">";
            }

            // ★ 函数类型：T()  /  T(A, B)
            if (check(TokenType::LParen)){
                advance();   // (
                tn += "(";
                if (!check(TokenType::RParen)){
                    tn += parseTypeName();
                    if (check(TokenType::Identifier)){
                        advance();
                    }
                    while (match(TokenType::Comma)){
                        tn += ", " + parseTypeName();
                        if (check(TokenType::Identifier)){
                            advance();
                        }
                    }
                }
                eat(TokenType::RParen);
                tn += ")";
            }
        } else {
            raise(EX_TYPE, "Expected type name", current().line);
        }

        // 数组后缀
        while (check(TokenType::LBracket) && peek(1).type == TokenType::RBracket){
            advance(); advance();
            tn = "List<" + tn + ">";
        }

        return tn;
    }

    static std::string sanitizeModuleName(const std::string& moduleName){
        std::string r = "vox_mod_";
        for (char c : moduleName){
            if (c == '.' || c == '/' || c == '\\' || c == ':'){
                r += '_';
            } else if (std::isalnum((unsigned char)c) || c == '_'){
                r += c;
            } else {
                r += '_';
            }
        }
        return r;
    }

    // ---------------- using ----------------
    void parseUsing(std::shared_ptr<ProgramNode> program){
        // ★ 关键：先把"到目前为止已解析的顶层符号"灌进表
        //   否则新加载的子模块继承到的是空表，看不到当前文件的定义
        collectNewSignatures(program);

        int usingLine = current().line;
        advance(); // using

        std::vector<Token> parts;
        while (!check(TokenType::Semicolon) && !check(TokenType::END)){
            parts.push_back(advance());
        }
        if (!check(TokenType::Semicolon)){
            raise(EX_SYNTAX, "Expected ';' after 'using'", usingLine);
        }
        eat(TokenType::Semicolon);

        bool importAll = false;
        if (parts.size() >= 2 &&
            parts.back().type == TokenType::Mul &&
            parts[parts.size()-2].type == TokenType::Dot){
            importAll = true;
            parts.pop_back();
            parts.pop_back();
        }

        std::string moduleName;
        for (const auto& p : parts) moduleName += p.value;
        if (moduleName.empty()){
            raise(EX_SYNTAX, "Expected module name after 'using'", usingLine);
        }

        if (moduleName == "Code"){
            importAll = true;
        }

        // ★ 归一化：把 "a.b._init_" 变成 "a.b"

        
        //   `using Code.window._init_` 和 `using Code.window` 映射到同一命名空间
        const std::string initSuffix = "._init_";
        if (moduleName.size() > initSuffix.size() &&
        moduleName.compare(moduleName.size() - initSuffix.size(),
        initSuffix.size(), initSuffix) == 0){
            moduleName = moduleName.substr(0, moduleName.size() - initSuffix.size());
        }
        
                if (importAll){
            _importAllNs.insert(sanitizeModuleName(moduleName));
        }
        
        std::string nsName = sanitizeModuleName(moduleName);

        // 短名别名
        _namespaceAliases[lastSegment(moduleName)] = nsName;

        // ★ 完整路径别名
        {
            std::string stripped = moduleName;
            const std::string codePrefix = "Code.";
            if (stripped.size() > codePrefix.size() &&
                stripped.compare(0, codePrefix.size(), codePrefix) == 0){
                stripped = stripped.substr(codePrefix.size());
            }
            if (stripped != lastSegment(moduleName) && !stripped.empty()){
                _namespaceAliases[stripped] = nsName;
            }
        }

        _knownNamespaces.insert(nsName);

        std::string basePath = moduleName;
        std::replace(basePath.begin(), basePath.end(), '.', '/');

        processImport(program, moduleName, nsName, basePath, importAll, usingLine);
    }

    // 递归收集 AST 里所有 ClassNode 的名字到 _knownClasses
    void registerClassesRecursive(const AstNodePtr& node,
                                  const std::string& nsPrefix = ""){
        if (!node) return;
        if (auto cls = std::dynamic_pointer_cast<ClassNode>(node)){
            _knownClasses.insert(cls->name);
            if (!nsPrefix.empty() && !_classToNamespace.count(cls->name)){
                _classToNamespace[cls->name] = nsPrefix;
            }
            return;
        }
        if (auto ns = std::dynamic_pointer_cast<NamespaceNode>(node)){
            std::string newPrefix = nsPrefix.empty()
                                  ? ns->name
                                  : (nsPrefix + "::" + ns->name);
            for (const auto& s : ns->statements)
                registerClassesRecursive(s, newPrefix);
            return;
        }
        if (auto prog = std::dynamic_pointer_cast<ProgramNode>(node)){
            for (const auto& s : prog->statements)
                registerClassesRecursive(s, nsPrefix);
            return;
        }
    }

    // ★ 递归收集类成员 + 命名空间成员
    void collectMembers(const AstNodePtr& node, const std::string& nsName){
        if (!node) return;

        if (auto cls = std::dynamic_pointer_cast<ClassNode>(node)){
            std::set<std::string> ms;
            std::set<std::string> ifs;
            std::map<std::string, std::string> ftypes;   // ★
            for (const auto& f : cls->fields){
                ms.insert(f.name);
                if (!f.isStatic){
                    ifs.insert(f.name);
                    ftypes[f.name] = f.typeName;   // ★
                }
            }
            for (const auto& m : cls->methods){
                ms.insert(m.name);
                if (m.isCtor) ms.insert("_init_");
                if (m.isDtor) ms.insert("_del_");
            }
            ms.insert("vox_type_name");
            ms.insert("vox_is_type");
            if (cls->isStruct) ms.insert("toString");
            _classMembers[cls->name] = ms;
            _classInstanceFields[cls->name] = ifs;
            _classFieldTypes[cls->name] = std::move(ftypes);   // ★
            return;
        }

        if (auto ns = std::dynamic_pointer_cast<NamespaceNode>(node)){
            std::set<std::string> ms;
            for (const auto& s : ns->statements){
                if (auto f = std::dynamic_pointer_cast<FunctionDeclNode>(s)){
                    ms.insert(f->name);
                } else if (auto c = std::dynamic_pointer_cast<ClassNode>(s)){
                    ms.insert(c->name);
                    collectMembers(s, ns->name);
                } else if (auto v = std::dynamic_pointer_cast<VariableDeclNode>(s)){
                    for (const auto& n : v->names){
                        ms.insert(n);
                        // ★ 记录变量类型
                        _namespaceVars[ns->name][n] = v->typeName;
                    }
                } else if (auto e = std::dynamic_pointer_cast<EnumNode>(s)){
                    ms.insert(e->name);
                } else if (auto eb = std::dynamic_pointer_cast<ExternBlockNode>(s)){
                    for (const auto& d : eb->decls){
                        if (auto fn = std::dynamic_pointer_cast<FunctionDeclNode>(d)){
                            ms.insert(fn->name);
                        }
                    }
                } else if (auto sub = std::dynamic_pointer_cast<NamespaceNode>(s)){
                    ms.insert(sub->name);
                    collectMembers(s, ns->name);
                }
            }
            _namespaceMembers[ns->name] = ms;
            return;
        }

        if (auto prog = std::dynamic_pointer_cast<ProgramNode>(node)){
            for (const auto& s : prog->statements){
                collectMembers(s, nsName);
            }
            return;
        }
    }

    // 递归收集 AST 里所有顶层变量名到 _globalVarNames
    void collectGlobalVars(const AstNodePtr& node){
        if (!node) return;
        if (auto var = std::dynamic_pointer_cast<VariableDeclNode>(node)){
            for (const auto& n : var->names){
                _globalVarNames.insert(n);
            }
            return;
        }
        if (auto ns = std::dynamic_pointer_cast<NamespaceNode>(node)){
            for (const auto& s : ns->statements) collectGlobalVars(s);
            return;
        }
        if (auto prog = std::dynamic_pointer_cast<ProgramNode>(node)){
            for (const auto& s : prog->statements) collectGlobalVars(s);
            return;
        }
    }

    // 把单个 .v/.voxs 文件解析到 NamespaceNode 里（递归）
    void inlineFile(const std::shared_ptr<NamespaceNode>& ns, const std::string& filePath){
        std::string content = readFile(filePath);
        if (content.empty()) return;

        Lexer subLexer(content, filePath);
        auto subTokens = subLexer.tokenize();

        namespace fs = std::filesystem;
        std::string subBase = fs::path(filePath).parent_path().string();

        Parser subParser(subTokens, content, filePath, subBase, &_importedModules, _searchDirs);

        // ★ 让子 Parser 继承父 Parser 已经积累的符号表
        //   （这样 pathlib 里引用 Code._init_.v 定义的 File 也能被识别）
        subParser._knownClasses       = _knownClasses;
        subParser._knownInterfaces    = _knownInterfaces;
        subParser._knownEnums         = _knownEnums;
        subParser._knownExceptions    = _knownExceptions;
        subParser._knownNamespaces    = _knownNamespaces;
        subParser._classToNamespace   = _classToNamespace;
        subParser._namespaceAliases   = _namespaceAliases;
        subParser._voxFunctionNames   = _voxFunctionNames;
        subParser._functionSignatures = _functionSignatures;
        subParser._genericFunctions   = _genericFunctions;   // ★ 继承父 Parser 已知泛型函数
        subParser._globalVarNames     = _globalVarNames;        // ★

        subParser._importAllNs.clear();   // ★ 让 subParser 从空开始，只记录它自己用 .* 导入的

        auto subProgram = std::dynamic_pointer_cast<ProgramNode>(subParser.parse());
        // ★ 把 subParser 用 .* 导入的命名空间，作为 ns 内部的 using namespace
        for (const auto& u : subParser._importAllNs){
            ns->internalUsings.push_back(u);
        }
        // ★ 把子 Parser 里新发现的泛型函数合并回来
        _genericFunctions.insert(subParser._genericFunctions.begin(),
                                subParser._genericFunctions.end());

        if (subProgram){
            // 关键：把子 Parser 里解析出的类名合并到本 Parser 的 _knownClasses
            registerClassesRecursive(subProgram, ns->name);

            for (auto& stmt : subProgram->statements){
                if (std::dynamic_pointer_cast<EndNode>(stmt)) continue;

                if (auto subNs = std::dynamic_pointer_cast<NamespaceNode>(stmt)){
                    // ★ importAll 的 NamespaceNode 不提升 —— 让它生成
                    //   `using namespace X;` 出现在**当前模块的命名空间内部**，
                    //   这样模块内的类才能看到 X 的符号
                    if (_topLevelProgram && !subNs->importAll){
                        _topLevelProgram->add(subNs);
                    } else {
                        ns->add(stmt);
                    }
                } else if (auto subEx = std::dynamic_pointer_cast<ExternBlockNode>(stmt)){
                    if (_topLevelProgram){
                        _topLevelProgram->add(subEx);
                    } else {
                        ns->add(stmt);
                    }
                } else {
                    ns->add(stmt);
                }
            }
        }
        _importedModules.insert(subParser.importedModules().begin(),
                        subParser.importedModules().end());

        // ★ 收集 subProgram 里所有顶层变量名（递归 namespace）
        collectGlobalVars(subProgram);

        // ★ 合并 subParser 的符号表回父 —— 让后续的 `using` 语句
        //   立刻能看到本模块引入的符号（不再等 collectSignatures）
        for (const auto& kv : subParser._functionSignatures){
            auto& dst = _functionSignatures[kv.first];
            for (const auto& sig : kv.second){
                dst.push_back(sig);
            }
        }
        for (const auto& c : subParser._knownClasses)      _knownClasses.insert(c);
        for (const auto& c : subParser._knownInterfaces)   _knownInterfaces.insert(c);
        for (const auto& c : subParser._knownEnums)        _knownEnums.insert(c);
        for (const auto& c : subParser._knownExceptions)   _knownExceptions.insert(c);
        for (const auto& c : subParser._knownNamespaces)   _knownNamespaces.insert(c);
        for (const auto& kv : subParser._classToNamespace) _classToNamespace.insert(kv);
        for (const auto& kv : subParser._namespaceAliases) _namespaceAliases.insert(kv);
    }

    void processImport(std::shared_ptr<ProgramNode> program,
                       const std::string& moduleName,
                       const std::string& nsName,
                       const std::string& basePath,
                       bool importAll,
                       int usingLine){
        
        if (!_importedModules.insert(nsName).second){
            // ★ 已导入：不需要 placeholder
            //   placeholder 会生成空的 `namespace X {}`，遮蔽外层的 X
            //   `using namespace X;` 由 parseUsing 里的 _importAllNs 记录，
            //   inlineFile 会把它加进当前模块的 internalUsings
            return;
        }

        namespace fs = std::filesystem;
        static const std::vector<std::string> extensions = {
            ".v", ".voxs", ".h", ".hpp", ".c", ".cpp"
        };
        bool found = false;

        // 1) 单个文件：<base>/<basePath><ext>
        for (const auto& ext : extensions){
            std::string candidate = findFile(basePath + ext);      // ★ 改
            if (!candidate.empty()){
                found = true;
                bool local = (ext == ".v" || ext == ".voxs");
                if (local){
                    auto ns = std::make_shared<NamespaceNode>(nsName, importAll);
                    program->add(ns);                // ★ 先加本模块
                    inlineFile(ns, candidate);        // ★ 后 inline，提升的 subNs 加在其后
                } else {
                    program->add(std::make_shared<ImportNode>(moduleName, candidate, false, importAll));
                }
                break;   // 找到就停
            }
        }

        // 2) 目录：只认 _init_.v / _init_.voxs
        if (!found){
            auto inits = findInitFiles(basePath);
            if (!inits.empty()){
                auto ns = std::make_shared<NamespaceNode>(nsName, importAll);
                program->add(ns);                 // ★ 先加本模块
                for (const auto& f : inits){
                    inlineFile(ns, f);            // ★ 后 inline
                }
                found = true;
            }
        }

        if (!found){
            raise(EX_FILE_NOT_FOUND, "Cannot find module '" + moduleName + "'", usingLine);
        }
    }

    static std::string readFile(const std::string& path){
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open()) return "";
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    // ---------------- namespace ----------------
    AstNodePtr parseNamespace(){
        int ln = current().line;
        advance(); // namespace
        if (!check(TokenType::Identifier)){
            raise(EX_SYNTAX, "Expected namespace name", ln);
        }
        std::string name = advance().value;
        eat(TokenType::LBrace);
        auto ns = std::make_shared<NamespaceNode>(name, false);
        while (!check(TokenType::RBrace) && !check(TokenType::END)){
            if (check(TokenType::KwClass)){
                auto cls = std::dynamic_pointer_cast<ClassNode>(parseClass());
                ns->add(cls);
            } else if (check(TokenType::KwNamespace)){
                ns->add(parseNamespace());
            } else if (check(TokenType::KwExtern)){
                ns->add(parseExtern());
            } else if (startsDeclaration()){
                bool isConst = match(TokenType::KwConst);
                std::string tn = parseTypeName();
                if (!check(TokenType::Identifier)){
                    raise(EX_SYNTAX, "Expected name", current().line);
                }
                Token nTok = advance();
                if (check(TokenType::LParen)){
                    ns->add(parseFunctionDeclaration(tn, nTok.value));
                } else {
                    std::vector<std::string> names { nTok.value };
                    std::vector<AstNodePtr> values;
                    if (match(TokenType::Equal)) values.push_back(parseExpression());
                    else values.push_back(defaultValue(tn));
                    while (match(TokenType::Comma)){
                        Token n = advance();
                        names.push_back(n.value);
                        if (match(TokenType::Equal)) values.push_back(parseExpression());
                        else values.push_back(defaultValue(tn));
                    }
                    eat(TokenType::Semicolon);
                    ns->add(std::make_shared<VariableDeclNode>(tn, names, values, isConst, nTok.line));
                }
            } else {
                raise(EX_SYNTAX, "Expected declaration in namespace", current().line);
            }
        }
        eat(TokenType::RBrace);
        return ns;
    }

    // ========== 语义检查：函数/lambda 返回值 ==========

    static bool stmtAlwaysReturns(const AstNodePtr& node);
    static bool blockAlwaysReturns(const std::shared_ptr<BlockNode>& blk);

    // 递归：检查所有函数 / lambda / 方法
    void checkReturnStatements(const AstNodePtr& node){
        if (!node) return;

        if (auto lam = std::dynamic_pointer_cast<LambdaNode>(node)){
            if (lam->returnType != "void"
                && lam->body && !lam->body->statements.empty()
                && !blockAlwaysReturns(lam->body)){
                raise(EX_TYPE,
                      "lambda declared to return '" + lam->returnType
                      + "' must return a value",
                      lam->line);
            }
        }
        if (auto fn = std::dynamic_pointer_cast<FunctionDeclNode>(node)){
            // extern / 只有签名 → 跳过
            if (!fn->isExtern
                && fn->body && !fn->body->statements.empty()
                && fn->returnType != "void"
                && !blockAlwaysReturns(fn->body)){
                raise(EX_TYPE,
                      "function '" + fn->name + "' declared to return '"
                      + fn->returnType + "' must return a value",
                      fn->line);
            }
        }
        if (auto nf = std::dynamic_pointer_cast<NestedFunctionNode>(node)){
            if (nf->returnType != "void"
                && nf->body && !nf->body->statements.empty()
                && !blockAlwaysReturns(nf->body)){
                raise(EX_TYPE,
                      "nested function '" + nf->name
                      + "' declared to return '" + nf->returnType
                      + "' must return a value",
                      0);
            }
        }
        if (auto cls = std::dynamic_pointer_cast<ClassNode>(node)){
            // interface 里的方法都是纯签名，跳过整段
            if (!cls->isInterface){
                for (const auto& m : cls->methods){
                    if (m.isCtor) continue;
                    if (m.returnType.empty() || m.returnType == "void") continue;
                    // 只有签名（空 body）→ 跳过
                    if (!m.body || m.body->statements.empty()) continue;
                    if (!blockAlwaysReturns(m.body)){
                        raise(EX_TYPE,
                              "method '" + cls->name + "." + m.name
                              + "' declared to return '" + m.returnType
                              + "' must return a value",
                              m.line);
                    }
                }
            }
        }

        // 递归所有子节点
        for (const auto& c : getChildren(node)){
            checkReturnStatements(c);
        }
    }

    // ========== 语义检查：顶层变量重名 ==========

    void checkTopLevelVarDuplicates(const std::shared_ptr<ProgramNode>& program){
        std::set<std::string> seen;
        for (const auto& stmt : program->statements){
            if (auto var = std::dynamic_pointer_cast<VariableDeclNode>(stmt)){
                for (const auto& n : var->names){
                    if (!seen.insert(n).second){
                        raise(EX_NAME,
                              "top-level variable '" + n
                              + "' is already declared",
                              var->line);
                    }
                }
            }
        }
    }

    // ---------------- extern ----------------
    AstNodePtr parseExtern(){
        int ln = current().line;
        advance(); // extern
        auto block = std::make_shared<ExternBlockNode>();
        if (match(TokenType::LBrace)){
            while (!check(TokenType::RBrace) && !check(TokenType::END)){
                // ★ 可选 template 前缀
                std::vector<std::string> tmplParams;
                if (check(TokenType::KwTemplate)){
                    tmplParams = parseTemplatePrefix();
                }

                std::string tn = parseTypeName();
                if (!check(TokenType::Identifier)){
                    raise(EX_SYNTAX, "Expected function name in extern block", current().line);
                }
                Token nTok = advance();
                auto params = parseParamList();
                eat(TokenType::Semicolon);
                auto fn = std::make_shared<FunctionDeclNode>(
                    tn, nTok.value, params, nullptr, true);
                fn->templateParams = std::move(tmplParams);
                block->add(fn);
            }
            eat(TokenType::RBrace);
        } else {
            std::string tn = parseTypeName();
            if (!check(TokenType::Identifier)){
                raise(EX_SYNTAX, "Expected function name after 'extern'", ln);
            }
            Token nTok = advance();
            auto params = parseParamList();
            eat(TokenType::Semicolon);
            block->add(std::make_shared<FunctionDeclNode>(
                tn, nTok.value, params, nullptr, true));
        }
        return block;
    }

    // ---------------- raise ----------------
    AstNodePtr parseRaise(){
        int raiseLine = current().line;
        int raiseCol = current().col;
        std::string srcLine = getLine(raiseLine);

        advance();   // raise

        if (!check(TokenType::Identifier)){
            raise(EX_TYPE, "Expected exception name after 'raise'", raiseLine);
        }
        Token nameToken = advance();
        std::string exceptionName = nameToken.value;
        int nameCol = nameToken.col;

        // 检查异常名合法
        bool known = _knownExceptions.count(exceptionName) > 0;
        if (!known){
            for (const auto& e : exceptions){
                if (e == exceptionName){ known = true; break; }
            }
        }
        if (!known){
            raise(EX_NAME, "'" + exceptionName + "' is not a known exception type",
                nameToken.line);
        }

        eat(TokenType::LParen);
        AstNodePtr msg = parseExpression();
        eat(TokenType::RParen);

        // 计算 span
        // span 1：raise 关键字（5 字符）
        int col1Start = raiseCol;
        int col1Len = 5;
        // span 2：异常表达式，从异常名到最后一个 ')' 前（找 ';' 之前的位置）
        int col2Start = nameCol;
        int semiPos = (int)srcLine.find(';', nameCol);
        if (semiPos < 0) semiPos = (int)srcLine.size();
        int col2Len = semiPos - col2Start;
        if (col2Len <= 0) col2Len = (int)exceptionName.size();

        eat(TokenType::Semicolon);

        // ★ 收集所有作用域变量（去重）
        std::vector<std::string> locals;
        std::set<std::string> seen;
        for (const auto& scope : _scopeVarStack){
            for (const auto& v : scope){
                if (seen.insert(v).second){
                    locals.push_back(v);
                }
            }
        }

        return std::make_shared<RaiseNode>(
            exceptionName, msg,
            _fileName, srcLine, raiseLine,
            col1Start, col1Len, col2Start, col2Len,
            locals);
    }

    // ---------------- lambda ----------------
    AstNodePtr parseLambda(){
        int startLine = current().line;          // ★
        std::string retType = advance().value;   // 返回类型关键字

        eat(TokenType::LParen);
        std::vector<LambdaParam> params;

        if (!check(TokenType::RParen)){
            do {
                LambdaParam p;
                // ★ 参数 const：`const T arg`
                if (check(TokenType::KwConst)){ p.isConst = true; advance(); }
                p.type = parseTypeName();
                if (check(TokenType::Identifier)){
                    p.name = advance().value;
                } else {
                    p.name = "_" + std::to_string(params.size());
                }
                // ★ lambda 参数约束
                if (match(TokenType::ArrowRight)){
                    p.constraint = parseConstraint();
                }
                params.push_back(p);
            } while (match(TokenType::Comma));
        }
        eat(TokenType::RParen);

        eat(TokenType::Arrow);       // =>

        // ★ 检测可选的 ref 标记
        bool byRef = false;
        if (match(TokenType::KwRef)){
            byRef = true;
        }

        eat(TokenType::LBrace);

        // ★ 把 lambda 参数加入作用域
        _scopeVarStack.push_back({});
        for (const auto& p : params){
            _scopeVarStack.back().push_back(p.name);
        }

        auto body = std::make_shared<BlockNode>();
        parseBlockInto(body);

        _scopeVarStack.pop_back();   // ★ 弹出

        eat(TokenType::RBrace);

        return std::make_shared<LambdaNode>(retType, params, body, byRef, startLine);
    }

    // ---------------- class ----------------
    AstNodePtr parseClass(bool isInterface = false){
        int classLine = current().line;
        advance();
        
        if (!check(TokenType::Identifier)){
            raise(EX_SYNTAX, "Expected class name", classLine);
        }
        Token nameTok = advance();

        // ★ 关键：在解析类体之前就注册类名，
        //   这样类体内 self / Class.method 的静态访问才能被识别为 ::
        _knownClasses.insert(nameTok.value);   // ★ 提前注册（双保险，防止 prescan 遗漏）

        auto cls = std::make_shared<ClassNode>(nameTok.value, std::vector<std::string>{});

        if (match(TokenType::LParen)){
            if (!check(TokenType::RParen)){
                do {
                    // ★ 支持 a.b.C 形式的基类名
                    if (!check(TokenType::Identifier)){
                        raise(EX_SYNTAX, "Expected base class name", current().line);
                    }
                    Token btok = advance();
                    std::string rawPath = btok.value;      // ★ 保留原路径
                    std::string baseName = rawPath;

                    // 展开命名空间别名（edit → vox_mod_Code_edit）
                    auto nsIt = _namespaceAliases.find(baseName);
                    if (nsIt != _namespaceAliases.end()){
                        baseName = nsIt->second;
                    }

                    // ★ 支持多级别名：window.ui.Base
                    while (check(TokenType::Dot) && peek(1).type == TokenType::Identifier){
                        advance();                 // 吃 .
                        Token seg = advance();

                        // ★ 先尝试拼多级别名："window" + "." + "ui" = "window.ui"
                        std::string candidate = rawPath + "." + seg.value;
                        auto aliasIt = _namespaceAliases.find(candidate);
                        if (aliasIt != _namespaceAliases.end()){
                            baseName = aliasIt->second;
                            rawPath = candidate;
                            continue;
                        }

                        // 否则按原逻辑拼
                        baseName += "::" + seg.value;
                        rawPath = candidate;
                    }

                    cls->bases.push_back(baseName);

                    // ★ 判断是否 interface：名字要能对上
                    std::string shortName = baseName;
                    auto pos = shortName.rfind("::");
                    if (pos != std::string::npos) shortName = shortName.substr(pos + 2);

                    bool isIface = _knownInterfaces.count(shortName) > 0
                                || _knownInterfaces.count(baseName) > 0;
                    cls->baseIsInterface.push_back(isIface);
                } while (match(TokenType::Comma));
            }
            eat(TokenType::RParen);
        }

        for (const auto& b : cls->bases){
            if (b == "Exception"){
                _knownExceptions.insert(nameTok.value);
            }
        }
        
        // ★ 记录当前类名 + 登记基类映射（供解析类体内 super 使用）
        std::string savedCls = _currentClassName;
        _currentClassName = nameTok.value;

        for (std::size_t i = 0; i < cls->bases.size(); ++i){
            bool isIface = (i < cls->baseIsInterface.size())
                           ? cls->baseIsInterface[i] : false;
            if (!isIface){
                _classBaseMap[nameTok.value] = cls->bases[i];
                break;
            }
        }

        eat(TokenType::LBrace);

        std::string currentAccess = "public";
        while (!check(TokenType::RBrace) && !check(TokenType::END)){
            // 先看注解
            std::vector<Attribute> memberAttrs;
            if (check(TokenType::At)){
                memberAttrs = parseAttributes();
            }
            
            if (check(TokenType::KwPublic) || check(TokenType::KwPrivate) ||
                check(TokenType::KwProtected)){
                currentAccess = advance().value;
            }

            bool isStatic = false;
            bool isOverride = false;
            bool isAbstractMethod = false;
            bool isFieldConst = false;                        // ★
            while (check(TokenType::KwStatic) || check(TokenType::KwOverride)
                || check(TokenType::KwAbstract) || check(TokenType::KwConst)){
                if (check(TokenType::KwStatic)){ isStatic = true; advance(); }
                else if (check(TokenType::KwOverride)){ isOverride = true; advance(); }
                else if (check(TokenType::KwAbstract)){ isAbstractMethod = true; advance(); }
                else { isFieldConst = true; advance(); }      // ★ 吃 const
            }

            std::string returnType;
            bool haveType = false;

            if (check(TokenType::KwSelf)){
                returnType = nameTok.value;
                advance();
                haveType = true;
            } else if (isContainerStart(current().type)){
                returnType = parseTypeName();
                haveType = true;
            } else if (isTypeKeyword(current().type)){
                returnType = parseTypeName();
                haveType = true;
            } else if (check(TokenType::Identifier)){
                std::size_t i = 1;
                while (_pos + i + 1 < _tokens.size() &&
                    _tokens[_pos + i].type == TokenType::Dot &&
                    _tokens[_pos + i + 1].type == TokenType::Identifier){
                    i += 2;
                }
                if (_pos + i < _tokens.size() &&
                    _tokens[_pos + i].type == TokenType::Identifier){
                    returnType = parseTypeName();
                    haveType = true;
                }
            }

            if (!check(TokenType::Identifier)){
                raise(EX_SYNTAX, "Expected member name in class body", current().line);
            }
            Token memName = advance();

            if (check(TokenType::LParen)){
                ClassMethod m;
                m.access = currentAccess;
                m.isStatic = isStatic;
                m.isOverride = isOverride;
                m.isCtor = (memName.value == "_init_");
                m.isDtor = (memName.value == "_del_");   // ★ 新增
                m.isAbstract = isAbstractMethod;
                m.returnType = m.isCtor ? "" : returnType;
                m.name = m.isCtor ? nameTok.value : memName.value;
                m.line = memName.line;
                m.params = parseParamList();

                // ★ 成员函数 const：`) const { ... }`
                if (match(TokenType::KwConst)){
                    m.isConst = true;
                    // 构造函数 / 析构函数不能是 const
                    if (m.isCtor){
                        raise(EX_TYPE,
                            "constructor cannot be declared const",
                            m.line);
                    }
                    if (m.isDtor){
                        raise(EX_TYPE,
                            "destructor cannot be declared const",
                            m.line);
                    }
                }

                // ★ _del_ 签名校验
                if (m.isDtor){
                    if (!m.params.empty()){
                        raise(EX_TYPE,
                            cls->name + "._del_() must take 0 parameters",
                            m.line);
                    }
                    if (!m.returnType.empty() && m.returnType != "void"){
                        raise(EX_TYPE,
                            cls->name + "._del_() must return 'void'",
                            m.line);
                    }
                    if (isInterface){
                        raise(EX_TYPE,
                            "interface '" + cls->name + "' cannot declare '_del_'",
                            m.line);
                    }
                }

                if (isAbstractMethod){
                    // ★ abstract 方法必须只有签名
                    if (!match(TokenType::Semicolon)){
                        raise(EX_TYPE,
                              "abstract method '" + m.name + "' must not have a body",
                              m.line);
                    }
                    m.body = std::make_shared<BlockNode>();
                } else if (match(TokenType::Semicolon)){
                    m.body = std::make_shared<BlockNode>();
                } else {
                    eat(TokenType::LBrace);
                    // ★ 把方法参数加入作用域
                    _scopeVarStack.push_back({});
                    for (const auto& p : m.params){
                        _scopeVarStack.back().push_back(p.name);
                    }
                    m.body = std::make_shared<BlockNode>();
                    parseBlockInto(m.body);
                    _scopeVarStack.pop_back();   // ★ 弹出
                    eat(TokenType::RBrace);
                }
                cls->methods.push_back(std::move(m));
            } else {
                if (!haveType){
                    raise(EX_SYNTAX, "Expected field type", current().line);
                }

                // 第一个字段
                ClassField f;
                f.access = currentAccess;
                f.isStatic = isStatic;
                f.isConst = isFieldConst;              // ★
                f.typeName = returnType;
                f.name = memName.value;
                f.init = nullptr;
                // ★ 字段约束
                if (match(TokenType::ArrowRight)){
                    f.constraint = parseConstraint();
                }
                if (match(TokenType::Equal)){
                    f.init = parseExpression();
                }
                cls->fields.push_back(std::move(f));

                // 后续字段：, name [= init]
                while (match(TokenType::Comma)){
                    if (!check(TokenType::Identifier)){
                        raise(EX_SYNTAX,
                              "Expected field name after ','",
                              current().line);
                    }
                    Token n = advance();
                    ClassField f2;
                    f2.access = currentAccess;
                    f2.isStatic = isStatic;
                    f2.isConst = isFieldConst;         // ★
                    f2.typeName = returnType;
                    f2.name = n.value;
                    f2.init = nullptr;
                    // ★ 字段约束
                    if (match(TokenType::ArrowRight)){
                        f2.constraint = parseConstraint();
                    }
                    if (match(TokenType::Equal)){
                        f2.init = parseExpression();
                    }
                    cls->fields.push_back(std::move(f2));
                }
                eat(TokenType::Semicolon);
            }

            currentAccess = "public";
        }
                // ========== interface 检查 ==========
        if (isInterface){
            // 不能有字段
            if (cls->fields.size() > 0){
                raise(EX_TYPE,
                      "Interface '" + nameTok.value + "' cannot have fields",
                      classLine);
            }
            // 方法必须有签名无 body
            for (const auto& m : cls->methods){
                if (m.body && m.body->statements.size() > 0){
                    raise(EX_TYPE,
                          "Interface method '" + m.name + "' cannot have a body",
                          classLine);
                }
            }
        }

        // ========== 有 abstract 方法 → 类自动成为 abstract ==========
        for (const auto& m : cls->methods){
            if (m.isAbstract){ cls->isAbstract = true; break; }
        }

        // ========== 多继承限制 ==========
        int classBaseCount = 0;
        for (const auto& b : cls->bases){
            if (_knownInterfaces.count(b) == 0){
                classBaseCount = classBaseCount + 1;
            }
        }
        if (classBaseCount > 1){
            raise(EX_TYPE,
                "Class '" + nameTok.value + "' can inherit from at most one class; "
                "multiple inheritance is only allowed with interfaces",
                classLine);
        }
        eat(TokenType::RBrace);
        return cls;
    
    }

    // ---------------- 参数列表 ----------------
    std::vector<Param> parseParamList(){
        eat(TokenType::LParen);
        std::vector<Param> params;
        bool kwOnly = false;
        while (!check(TokenType::RParen) && !check(TokenType::END)){

            // '/' 分隔符：之前的都变成 positional-only
            if (check(TokenType::Div)){
                advance();
                match(TokenType::Comma);
                for (auto& p : params) p.isPosOnly = true;
                continue;
            }

            // 单独的 '*' 分隔符（后面跟 ',' 或 ')'）
            if (check(TokenType::Mul) &&
                (peek(1).type == TokenType::Comma || peek(1).type == TokenType::RParen)){
                advance();
                match(TokenType::Comma);
                kwOnly = true;
                continue;
            }

            Param p;
            p.isKwOnly = kwOnly;

            if (check(TokenType::Mul)){ p.isVariadic = true; advance(); }
            if (check(TokenType::Amp) || check(TokenType::KwRef)){ p.isRef = true; advance(); }

            // ★ 参数 const：`const T arg`
            if (check(TokenType::KwConst)){ p.isConst = true; advance(); }

            if (check(TokenType::KwSelf)) p.type = advance().value;
            else                          p.type = parseTypeName();

            if (check(TokenType::Amp) || check(TokenType::KwRef)){ p.isRef = true; advance(); }

            if (!check(TokenType::Identifier)){
                raise(EX_SYNTAX, "Expected parameter name", current().line);
            }
            p.name = advance().value;

            // ★ 参数约束：-> edit.Range[-10, 10]  —— 必须在吃完参数名之后
            if (match(TokenType::ArrowRight)){
                p.constraint = parseConstraint();
            }

            if (match(TokenType::Equal)){
                p.defaultValue = parseExpression();
            }

            params.push_back(std::move(p));
            if (!match(TokenType::Comma)) break;
        }
        eat(TokenType::RParen);
        return params;
    }

    void parseBlockInto(std::shared_ptr<BlockNode> body){
        _scopeVarStack.push_back({});
        while (!check(TokenType::RBrace) && !check(TokenType::END)){
            parseStatementInto(body);
        }
        _scopeVarStack.pop_back();
    }

    // 解析 `{ ... }` 或单个语句，把内容加到 body 里
    void parseBlockOrStatementInto(std::shared_ptr<BlockNode> body){
        if (match(TokenType::LBrace)){
            parseBlockInto(body);
            eat(TokenType::RBrace);
        } else {
            parseStatementInto(body);
        }
    }

    // 检测是否是嵌套函数：Type name(Params) 或 Type(Params) name(Params)
    bool startsNestedFunction() const {
        if (!isTypeKeyword(current().type) &&
            !isContainerStart(current().type) &&
            !check(TokenType::Identifier)) return false;

        std::size_t i = 1;

        // 若类型本身是函数类型：Type(...) name(...)
        if (_pos + i < _tokens.size() &&
            _tokens[_pos + i].type == TokenType::LParen){
            int depth = 0;
            while (_pos + i < _tokens.size()){
                TokenType t = _tokens[_pos + i].type;
                if (t == TokenType::END) return false;
                if (t == TokenType::LParen) depth++;
                else if (t == TokenType::RParen){
                    depth--;
                    if (depth == 0){ i++; break; }
                }
                i++;
            }
        }

        // 期待 Identifier（函数名）
        if (_pos + i >= _tokens.size() ||
            _tokens[_pos + i].type != TokenType::Identifier) return false;
        i++;

        // 然后期待 ( —— 参数列表开始
        if (_pos + i >= _tokens.size() ||
            _tokens[_pos + i].type != TokenType::LParen) return false;

        return true;
    }

    // 解析嵌套函数
    AstNodePtr parseNestedFunction(){
        std::string retType = parseTypeName();     // 返回类型
        std::string fname = advance().value;        // 函数名
        auto params = parseParamList();             // 参数列表

        bool byRef = false;
        if (match(TokenType::Arrow)){               // 可选 => 标记
            if (match(TokenType::KwRef)){
                byRef = true;
            }
        }

        eat(TokenType::LBrace);
        auto body = std::make_shared<BlockNode>();
        parseBlockInto(body);
        eat(TokenType::RBrace);

        return std::make_shared<NestedFunctionNode>(
            retType, fname, params, body, byRef);
    }

    // ---------------- for ----------------
    AstNodePtr parseFor(){
        advance();
        eat(TokenType::LParen);

        AstNodePtr init = nullptr, cond = nullptr, step = nullptr;

        if (!check(TokenType::Semicolon)){
            if (startsDeclaration()){
                bool isConst = match(TokenType::KwConst);
                init = parseDeclarationBody(isConst);
            } else {
                init = parseSimpleStatement();
            }
        }
        eat(TokenType::Semicolon);

        if (!check(TokenType::Semicolon)){
            cond = parseExpression();
        }
        eat(TokenType::Semicolon);

        if (!check(TokenType::RParen)){
            step = parseSimpleStatement();
        }
        eat(TokenType::RParen);

        auto body = std::make_shared<BlockNode>();
        parseBlockOrStatementInto(body);

        return std::make_shared<ForNode>(init, cond, step, body);
    }

    AstNodePtr parseForeach(){
        int ln = current().line;
        advance(); // foreach

        eat(TokenType::LParen);

        // 1) 声明列表：支持两种写法
        //    a) 每个变量带类型：  int i, string j
        //    b) 共享类型：        int i, j, k
        std::vector<std::pair<std::string,std::string>> decls;
        std::string lastType;
        do {
            std::string type;

            // 判断是不是共享类型：当前是 Identifier，后面紧跟 ; 或 ,，且已有 lastType
            bool sharedType = (!lastType.empty()
                               && check(TokenType::Identifier)
                               && (peek(1).type == TokenType::Semicolon
                                   || peek(1).type == TokenType::Comma));

            if (sharedType){
                type = lastType;
            } else {
                type = parseTypeName();
            }

            if (!check(TokenType::Identifier)){
                raise(EX_SYNTAX, "Expected variable name in foreach declaration", current().line);
            }
            Token nameTok = advance();
            decls.push_back({type, nameTok.value});
            lastType = type;
        } while (match(TokenType::Comma));

        eat(TokenType::Semicolon);

        // 2) in 子句列表： name in expr (, name in expr)*
        std::vector<ForeachVar> vars;
        do {
            if (!check(TokenType::Identifier)){
                raise(EX_SYNTAX, "Expected variable name in foreach 'in' clause", current().line);
            }
            Token nameTok = advance();
            std::string name = nameTok.value;

            if (!check(TokenType::KwIn)){
                raise(EX_SYNTAX, "Expected 'in' after variable name '" + name + "'", current().line);
            }
            advance(); // in

            AstNodePtr iterable = parseExpression();

            std::string type;
            bool found = false;
            for (const auto& d : decls){
                if (d.second == name){
                    type = d.first;
                    found = true;
                    break;
                }
            }
            if (!found){
                raise(EX_NAME, "'" + name + "' was not declared in foreach", nameTok.line);
            }

            vars.push_back({type, name, iterable});
        } while (match(TokenType::Comma));

        eat(TokenType::RParen);
        eat(TokenType::LBrace);

        // ★ 多变量时登记 optional 变量名
        std::vector<std::string> addedOptional;
        if (vars.size() > 1){
            for (const auto& v : vars){
                if (_optionalLoopVars.insert(v.name).second){
                    addedOptional.push_back(v.name);
                }
            }
        }

        // ★ 把 foreach 变量加入局部作用域
        _scopeVarStack.push_back({});
        for (const auto& v : vars){
            _scopeVarStack.back().push_back(v.name);
        }

        auto body = std::make_shared<BlockNode>();
        parseBlockInto(body);

        _scopeVarStack.pop_back();

        // ★ 恢复
        for (const auto& n : addedOptional){
            _optionalLoopVars.erase(n);
        }

        eat(TokenType::RBrace);
        return std::make_shared<ForeachNode>(vars, body);
    }

    // ---------------- if / else if / else ----------------
    AstNodePtr parseIf(){
        int ln = current().line;
        advance();
        eat(TokenType::LParen);
        AstNodePtr cond = parseExpression();
        eat(TokenType::RParen);

        auto thenBody = std::make_shared<BlockNode>();
        parseBlockOrStatementInto(thenBody);

        AstNodePtr elseBranch = nullptr;
        if (match(TokenType::KwElse)){
            if (check(TokenType::KwIf)){
                elseBranch = parseIf();
            } else {
                auto elseBody = std::make_shared<BlockNode>();
                parseBlockOrStatementInto(elseBody);
                elseBranch = elseBody;
            }
        }

        return std::make_shared<IfNode>(cond, thenBody, elseBranch);
    }

    // ---------------- while ----------------
    AstNodePtr parseWhile(){
        int ln = current().line;
        advance();
        eat(TokenType::LParen);
        AstNodePtr cond = parseExpression();
        eat(TokenType::RParen);

        auto body = std::make_shared<BlockNode>();
        parseBlockOrStatementInto(body);

        return std::make_shared<WhileNode>(cond, body);
    }

    // ---------------- do-while ----------------
    AstNodePtr parseDoWhile(){
        int ln = current().line;
        advance();

        auto body = std::make_shared<BlockNode>();
        parseBlockOrStatementInto(body);

        if (!check(TokenType::KwWhile)){
            raise(EX_SYNTAX, "Expected 'while' after 'do' block", ln);
        }
        advance();
        eat(TokenType::LParen);
        AstNodePtr cond = parseExpression();
        eat(TokenType::RParen);
        eat(TokenType::Semicolon);

        return std::make_shared<DoWhileNode>(body, cond);
    }
    
    // ---------------- try / catch / finally ----------------
    AstNodePtr parseTry(){
        int ln = current().line;
        advance();  // try

        // try 体
        eat(TokenType::LBrace);
        auto tryBody = std::make_shared<BlockNode>();
        parseBlockInto(tryBody);
        eat(TokenType::RBrace);

        // catch 子句（可多个）
        std::vector<CatchClause> catches;
        while (check(TokenType::KwCatch)){
            advance();  // catch
            eat(TokenType::LParen);

            if (!check(TokenType::Identifier)){
                raise(EX_SYNTAX, "Expected exception type in catch", current().line);
            }
            std::string excType = advance().value;

            std::string varName;
            if (check(TokenType::Identifier)){
                varName = advance().value;
            } else {
                varName = "_e";     // 默认名
            }
            eat(TokenType::RParen);

            eat(TokenType::LBrace);
            auto catchBody = std::make_shared<BlockNode>();
            parseBlockInto(catchBody);
            eat(TokenType::RBrace);

            catches.push_back({excType, varName, catchBody});
        }

        // finally
        std::shared_ptr<BlockNode> finallyBody = nullptr;
        if (match(TokenType::KwFinally)){
            eat(TokenType::LBrace);
            finallyBody = std::make_shared<BlockNode>();
            parseBlockInto(finallyBody);
            eat(TokenType::RBrace);
        }

        if (catches.empty() && !finallyBody){
            raise(EX_SYNTAX,
                "try must have at least one catch or finally",
                ln);
        }

        return std::make_shared<TryNode>(tryBody, catches, finallyBody);
    }

    void parseStatementInto(std::shared_ptr<BlockNode> body){
        if (check(TokenType::KwReturn)){
            int retLine = current().line;
            advance();
            bool isMainCtx = _semCurrentReturnType.empty();   // 顶层 = 隐式 void main
            if (match(TokenType::Semicolon)){
                auto ret = std::make_shared<ReturnNode>(nullptr, retLine);
                ret->inMain = isMainCtx;
                body->add(ret);
            } else {
                AstNodePtr e = parseExpression();
                eat(TokenType::Semicolon);
                body->add(std::make_shared<ReturnNode>(e, retLine));
            }
        } else if (check(TokenType::KwRaise)){
            body->add(parseRaise());
        } else if (check(TokenType::KwFor)){
            body->add(parseFor());
        } else if (check(TokenType::KwForeach)){
            body->add(parseForeach());
        } else if (check(TokenType::KwIf)){
            body->add(parseIf());
        } else if (check(TokenType::KwWhile)){
            body->add(parseWhile());
        } else if (check(TokenType::KwDo)){
            body->add(parseDoWhile());
        } else if (check(TokenType::KwTry)){
            body->add(parseTry());
        } else if (check(TokenType::KwSwitch)){
            body->add(parseSwitch());
        } else if (check(TokenType::KwWith)){          // ★
            body->add(parseWith());
        } else if (check(TokenType::KwBreak)){
            int ln = current().line;   // ★
            advance();
            eat(TokenType::Semicolon);
            body->add(std::make_shared<BreakNode>(ln));
        } else if (check(TokenType::KwContinue)){
            int ln = current().line;   // ★
            advance();
            eat(TokenType::Semicolon);
            body->add(std::make_shared<ContinueNode>(ln));
        } else if (startsDeclaration()){
            bool isConst = match(TokenType::KwConst);
            AstNodePtr decl = parseDeclarationBody(isConst);
            eat(TokenType::Semicolon);
            body->add(decl);

            // ★ 注册变量名
            if (auto v = std::dynamic_pointer_cast<VariableDeclNode>(decl)){
                if (!_scopeVarStack.empty()){
                    for (const auto& n : v->names){
                        _scopeVarStack.back().push_back(n);
                    }
                }
            }
        } else if (check(TokenType::Identifier)
                   || check(TokenType::KwSelf)
                   || check(TokenType::KwSuper)){   // ★ 加 KwSuper
            body->add(parseExpressionStatement());
        } else {
            raise(EX_SYNTAX,
                "Expected statement, but encountered "
                + tokenTypeName(current().type),
                current().line);
        }
    }

    // ---------------- switch / case / default ----------------
    AstNodePtr parseSwitch(){
        int ln = current().line;
        advance();  // switch
        eat(TokenType::LParen);
        AstNodePtr expr = parseExpression();
        eat(TokenType::RParen);
        eat(TokenType::LBrace);

        std::vector<SwitchCase> cases;
        while (!check(TokenType::RBrace) && !check(TokenType::END)){
            if (check(TokenType::KwCase)){
                advance();
                AstNodePtr value = parseExpression();
                eat(TokenType::Colon);
                auto body = std::make_shared<BlockNode>();
                // case 体：到下一个 case/default/} 为止
                while (!check(TokenType::KwCase) &&
                    !check(TokenType::KwDefault) &&
                    !check(TokenType::RBrace) &&
                    !check(TokenType::END)){
                    parseStatementInto(body);
                }
                cases.push_back({value, body});
            } else if (check(TokenType::KwDefault)){
                advance();
                eat(TokenType::Colon);
                auto body = std::make_shared<BlockNode>();
                while (!check(TokenType::KwCase) &&
                    !check(TokenType::RBrace) &&
                    !check(TokenType::END)){
                    parseStatementInto(body);
                }
                cases.push_back({nullptr, body});
            } else {
                raise(EX_SYNTAX,
                    "Expected 'case' or 'default' in switch body",
                    current().line);
            }
        }
        eat(TokenType::RBrace);

        return std::make_shared<SwitchNode>(expr, cases, ln);   // ★ 传 ln
    }

    // ---------------- with ----------------
    AstNodePtr parseWith(){
        int ln = current().line;
        advance();   // with
        eat(TokenType::LParen);

        std::string typeName;
        std::string varName;

        // 两种写法：
        //   with (Type name = init)
        //   with (name = init)              ← auto 推断
        if (check(TokenType::Identifier) &&
            peek(1).type == TokenType::Identifier){
            // 两个 Identifier：Type name
            typeName = advance().value;
            varName  = advance().value;
        } else if (check(TokenType::Identifier)){
            varName = advance().value;
        } else {
            raise(EX_SYNTAX,
                "Expected variable name in 'with'",
                current().line);
        }

        if (!check(TokenType::Equal)){
            raise(EX_SYNTAX,
                "Expected '=' in 'with' declaration",
                current().line);
        }
        advance();

        AstNodePtr init = parseExpression();
        eat(TokenType::RParen);

        eat(TokenType::LBrace);
        auto body = std::make_shared<BlockNode>();
        parseBlockInto(body);
        eat(TokenType::RBrace);

        return std::make_shared<WithNode>(typeName, varName, init, body);
    }

    // ---------------- 表达式 ----------------
    AstNodePtr parseExpression(){
        AstNodePtr cond = parsePipePipe();
        if (match(TokenType::Question)){
            AstNodePtr thenExpr = parseExpression();
            eat(TokenType::Colon);
            AstNodePtr elseExpr = parseExpression();
            return std::make_shared<TernaryNode>(cond, thenExpr, elseExpr);
        }
        return cond;
    }

    AstNodePtr parsePipePipe(){
        AstNodePtr left = parseAmpAmp();
        while (check(TokenType::PipePipe)){
            int opLine = current().line;
            TokenType op = advance().type;
            AstNodePtr right = parseAmpAmp();
            left = std::make_shared<BinaryNode>(left, op, right, opLine);
        }
        return left;
    }

    AstNodePtr parseAmpAmp(){
        AstNodePtr left = parsePipe();
        while (check(TokenType::AmpAmp)){
            int opLine = current().line;
            TokenType op = advance().type;
            AstNodePtr right = parsePipe();
            left = std::make_shared<BinaryNode>(left, op, right, opLine);
        }
        return left;
    }

    AstNodePtr parsePipe(){
        AstNodePtr left = parseCaret();
        while (check(TokenType::Pipe)){
            int opLine = current().line;
            TokenType op = advance().type;
            AstNodePtr right = parseCaret();
            left = std::make_shared<BinaryNode>(left, op, right, opLine);
        }
        return left;
    }

    AstNodePtr parseCaret(){
        AstNodePtr left = parseAmp();
        while (check(TokenType::Caret)){
            int opLine = current().line;
            TokenType op = advance().type;
            AstNodePtr right = parseAmp();
            left = std::make_shared<BinaryNode>(left, op, right, opLine);
        }
        return left;
    }

    AstNodePtr parseAmp(){
        AstNodePtr left = parseComparison();
        while (check(TokenType::Amp)){
            int opLine = current().line;
            TokenType op = advance().type;
            AstNodePtr right = parseComparison();
            left = std::make_shared<BinaryNode>(left, op, right, opLine);
        }
        return left;
    }

    AstNodePtr parseComparison(){
        AstNodePtr left = parseShift();
        while (check(TokenType::Less) || check(TokenType::Greater) ||
               check(TokenType::LessEqual) || check(TokenType::GreaterEqual) ||
               check(TokenType::EqualEqual) || check(TokenType::NotEqual) ||
               check(TokenType::KwIs)){
            int opLine = current().line;
            TokenType op = advance().type;

            if (op == TokenType::KwIs){
                if (check(TokenType::Null)){
                    advance();
                    left = std::make_shared<IsNullNode>(left);
                } else {
                    std::string typeName = parseTypeName();
                    left = std::make_shared<IsNode>(left, typeName, opLine);
                }
            } else {
                AstNodePtr right = parseShift();
                left = std::make_shared<BinaryNode>(left, op, right, opLine);
            }
        }
        return left;
    }

    AstNodePtr parseEnum(){
        int ln = current().line;
        advance();   // enum

        if (!check(TokenType::Identifier)){
            raise(EX_SYNTAX, "Expected enum name after 'enum'", ln);
        }
        std::string name = advance().value;

        eat(TokenType::LBrace);

        std::vector<EnumMember> members;
        int nextValue = 0;

        while (!check(TokenType::RBrace) && !check(TokenType::END)){
            if (!check(TokenType::Identifier)){
                raise(EX_SYNTAX, "Expected enum member name", current().line);
            }
            std::string memName = advance().value;

            EnumMember m;
            m.name = memName;
            m.value = nextValue;
            m.hasExplicitValue = false;

            if (match(TokenType::Equal)){
                // 显式值：解析常量表达式
                // 简化：只支持整数常量（或负整数）
                bool neg = false;
                if (match(TokenType::Sub)) neg = true;
                if (!check(TokenType::Number)){
                    raise(EX_SYNTAX, "Expected integer value in enum member", current().line);
                }
                Token numTok = advance();
                int val = std::stoi(numTok.value, nullptr, 0);
                if (neg) val = -val;
                m.value = val;
                m.hasExplicitValue = true;
            }

            nextValue = m.value + 1;
            members.push_back(m);

            if (!match(TokenType::Comma)) break;
        }

        eat(TokenType::RBrace);
        // enum 后不强制分号（Vox 风格）
        match(TokenType::Semicolon);

        // ★ 注册到 _knownEnums（类似 _knownClasses）
        _knownEnums.insert(name);
        for (const auto& m : members){
            _knownEnumMembers[name + "." + m.name] = name;
        }

        return std::make_shared<EnumNode>(name, members);
    }

    AstNodePtr parseShift(){
        AstNodePtr left = parseAdditive();
        while (check(TokenType::Shl) || check(TokenType::Shr)){
            int opLine = current().line;
            TokenType op = advance().type;
            AstNodePtr right = parseAdditive();
            left = std::make_shared<BinaryNode>(left, op, right, opLine);
        }
        return left;
    }

    AstNodePtr parseAdditive(){
        AstNodePtr left = parseMultiplicative();
        while (check(TokenType::Plus) || check(TokenType::Sub)){
            int opLine = current().line;
            TokenType op = advance().type;
            AstNodePtr right = parseMultiplicative();
            left = std::make_shared<BinaryNode>(left, op, right, opLine);
        }
        return left;
    }

    AstNodePtr parseMultiplicative(){
        AstNodePtr left = parseUnary();
        while (check(TokenType::Mul) || check(TokenType::Div) || check(TokenType::Mod)){
            int opLine = current().line;
            TokenType op = advance().type;
            AstNodePtr right = parseUnary();
            left = std::make_shared<BinaryNode>(left, op, right, opLine);
        }
        return left;
    }

    AstNodePtr parseUnary(){
        if (check(TokenType::Bang) || check(TokenType::Tilde) ||
            check(TokenType::Sub) || check(TokenType::Plus)){
            TokenType op = current().type;
            int opLine = current().line;
            advance();
            AstNodePtr operand = parseUnary();
            return std::make_shared<UnaryNode>(op, operand, opLine);
        }
        return parsePostfix(parsePrimary());
    }

    // ★ 创建 VariableNode：多变量 foreach 变量自动设 needsDeref
    std::shared_ptr<VariableNode> makeVariableNode(const std::string& name, int line){
        auto v = std::make_shared<VariableNode>(name, line);
        if (_optionalLoopVars.count(name)){
            v->needsDeref = true;
            v->isOptionalLoopVar = true;
        }
        return v;
    }

    AstNodePtr parsePostfix(AstNodePtr node){
        while (true){
            if (check(TokenType::LParen)){
                int callLine = current().line;
                advance();
                std::vector<AstNodePtr> args;
                if (!check(TokenType::RParen)){
                    do { args.push_back(parseArgument()); } while (match(TokenType::Comma));
                }
                eat(TokenType::RParen);
                node = std::make_shared<CallNode>(node, args, callLine);
            } else if (check(TokenType::Less)){
                // ★ 预扫描：只允许"类型名 token"，遇到别的立刻判定"不是泛型调用"
                //   避免 `while (i < n)` 这种误判
                std::size_t i = 1;
                int depth = 1;
                bool ok = true;
                while (_pos + i < _tokens.size() && depth > 0){
                    TokenType t = _tokens[_pos + i].type;
                    if (t == TokenType::Less) depth++;
                    else if (t == TokenType::Greater) depth--;
                    else if (t == TokenType::Shr) depth -= 2;
                    else if (t == TokenType::END) { ok = false; break; }
                    else if (t == TokenType::Identifier
                            || t == TokenType::Dot
                            || t == TokenType::Comma
                            || t == TokenType::LBracket
                            || t == TokenType::RBracket
                            || t == TokenType::Amp
                            || isTypeKeyword(t)
                            || isContainerStart(t)){
                        // 类型名允许的 token —— 继续
                    } else {
                        // 别的 token（`)` `;` `{` `=` 等）→ 肯定不是泛型调用
                        ok = false;
                        break;
                    }
                    i++;
                }
                if (ok && depth == 0 && _pos + i < _tokens.size() &&
                    _tokens[_pos + i].type == TokenType::LParen){
                    // 是泛型调用：foo<T, U>(...)
                    std::vector<std::string> tArgs;
                    advance();   // <
                    tArgs.push_back(parseTypeName());
                    while (match(TokenType::Comma)){
                        tArgs.push_back(parseTypeName());
                    }
                    eatGreater();   // >
                    eat(TokenType::LParen);
                    std::vector<AstNodePtr> args;
                    if (!check(TokenType::RParen)){
                        do { args.push_back(parseArgument()); } while (match(TokenType::Comma));
                    }
                    eat(TokenType::RParen);
                    node = std::make_shared<CallNode>(node, args, current().line, tArgs);
                } else {
                    break;   // 不是泛型调用，当小于号处理
                }
            } else if (match(TokenType::Dot)){
                if (!check(TokenType::Identifier)){
                    raise(EX_SYNTAX, "Expected identifier after '.'", current().line);
                }
                Token memberTok = advance();

                // ★ 先尝试拼多级别名： "window" + "." + "ui" = "window.ui"
                std::string joinedPath = pathTextOf(node);
                if (!joinedPath.empty()){
                    std::string candidate = joinedPath + "." + memberTok.value;
                    auto aliasIt = _namespaceAliases.find(candidate);
                    if (aliasIt != _namespaceAliases.end()){
                        node = makeVariableNode(aliasIt->second, memberTok.line);
                        continue;   // 下一个 .Xxx 走正常逻辑
                    }
                }

                // ★ 原有逻辑（加了"局部变量优先"检查）
                bool nsAccess = false;
                if (auto v = std::dynamic_pointer_cast<VariableNode>(node)){
                    // ★ 首先：如果这个名字在当前作用域里是局部变量，
                    //   那就绝不可能是命名空间 / 类名
                    bool isLocal = false;
                    for (auto it = _scopeVarStack.rbegin(); it != _scopeVarStack.rend(); ++it){
                        for (const auto& n : *it){
                            if (n == v->name){ isLocal = true; break; }
                        }
                        if (isLocal) break;
                    }

                    if (!isLocal){
                        // 1) 用户写的可能是别名（如 xml），替换成真实命名空间名（vox_mod_Code_xml）
                        auto it = _namespaceAliases.find(v->name);
                        if (it != _namespaceAliases.end()){
                            v->name = it->second;
                            nsAccess = true;
                        }
                        // 2) 用户也可能直接写真实命名空间名（如 vox_mod_math）
                        else if (_knownNamespaces.count(v->name) > 0){
                            nsAccess = true;
                        }
                        // 3) 已知类名 → 自动加 namespace 前缀（如 XmlNode → vox_mod_Code_xml::XmlNode）
                        else if (_knownClasses.count(v->name) > 0){
                            auto cit = _classToNamespace.find(v->name);
                            if (cit != _classToNamespace.end()){
                                v->name = cit->second + "::" + v->name;
                            }
                            nsAccess = true;
                        }
                        else if (_knownEnums.count(v->name) > 0){
                            nsAccess = true;
                        }
                    }
                } else if (auto m = std::dynamic_pointer_cast<MemberAccessNode>(node)){
                    // 4) node 是命名空间/类访问链
                    //    只有当它的 member 是大写开头（类型名）时，才继续用 ::
                    if (m->namespaceAccess &&
                        !m->member.empty() &&
                        std::isupper(static_cast<unsigned char>(m->member[0]))){
                        nsAccess = true;
                    }
                }
                node = std::make_shared<MemberAccessNode>(node, memberTok.value, nsAccess, memberTok.line);
            } else if (match(TokenType::LBracket)){
                int ln = current().line;
                AstNodePtr index = parseExpression();
                eat(TokenType::RBracket);
                node = std::make_shared<IndexNode>(node, index, ln);
            } else if (check(TokenType::PlusPlus) || check(TokenType::SubSub)){
                // ★ 后置自增/自减（只吃一次：不允许 i++++）
                int opLine = current().line;
                bool isDec = check(TokenType::SubSub);
                advance();
                node = std::make_shared<PostIncrementNode>(node, isDec, opLine);
                break;
            } else {
                break;
            }
        }
        return node;
    }

    AstNodePtr parsePrimary(){
        Token token = current();

        if (token.type == TokenType::Number){
            advance();
            return std::make_shared<NumberNode>(
                std::stoll(token.value, nullptr, 0),
                token.value);                     // ★ 传原始文本
        }
        if (token.type == TokenType::FloatLiteral){
            advance();
            std::string s = token.value;
            bool isFloat32 = false;
            if (!s.empty() && (s.back() == 'f' || s.back() == 'F')){
                isFloat32 = true;
                s.pop_back();
            }
            double v = std::stod(s);
            return std::make_shared<FloatNode>(v, isFloat32);
        }
        if (token.type == TokenType::Identifier){
            advance();
            // ★ 兜底：即便 lexer 没把 self / super 当关键字
            if (token.value == "self"){
                return std::make_shared<SelfNode>();
            }
            if (token.value == "super"){
                std::string base;
                auto it = _classBaseMap.find(_currentClassName);
                if (it != _classBaseMap.end()) base = it->second;
                if (base.empty()){
                    raise(EX_SYNTAX,
                          "'super' can only be used inside a class with a base class",
                          token.line);
                }
                return std::make_shared<SuperNode>(base);
            }
            return makeVariableNode(token.value, token.line);
        }
        if (token.type == TokenType::KwSelf){
            advance();
            return std::make_shared<SelfNode>();
        }
        if (token.type == TokenType::KwSuper){
            int ln = token.line;
            advance();
            std::string base;
            auto it = _classBaseMap.find(_currentClassName);
            if (it != _classBaseMap.end()) base = it->second;
            if (base.empty()){
                raise(EX_SYNTAX,
                      "'super' can only be used inside a class with a base class",
                      ln);
            }
            return std::make_shared<SuperNode>(base);
        }
        if (token.type == TokenType::KwSuper){
            int ln = token.line;
            advance();
            if (_knownClasses.count(_currentClassName) == 0){
                raise(EX_SYNTAX, "'super' can only be used inside a class", ln);
            }
            // 拿当前类的第一个非 interface 基类
            std::string base;
            auto it = _classBaseMap.find(_currentClassName);
            if (it != _classBaseMap.end()) base = it->second;
            if (base.empty()){
                raise(EX_SYNTAX,
                      "'" + _currentClassName + "' has no base class to call 'super' on",
                      ln);
            }
            return std::make_shared<SuperNode>(base);
        }
        if (token.type == TokenType::KwNew){
            advance();
            if (!check(TokenType::Identifier)){
                raise(EX_SYNTAX, "Expected class name after 'new'", current().line);
            }
            Token firstTok = advance();
            std::string rawPath = firstTok.value;    // ★ 保留原始路径
            std::string cls = rawPath;

            // 展开命名空间别名
            auto it = _namespaceAliases.find(cls);
            if (it != _namespaceAliases.end()){
                cls = it->second;
            }

            // ★ 支持多级别名 new a.b.C(...)
            while (check(TokenType::Dot) && peek(1).type == TokenType::Identifier){
                advance();  // 吃 .
                Token seg = advance();

                // ★ 先尝试拼多级别名："window" + "." + "ui" = "window.ui"
                std::string candidate = rawPath + "." + seg.value;
                auto aliasIt = _namespaceAliases.find(candidate);
                if (aliasIt != _namespaceAliases.end()){
                    cls = aliasIt->second;
                    rawPath = candidate;
                    continue;
                }

                // 否则按原逻辑拼
                cls += "::" + seg.value;
                rawPath = candidate;
            }
            
            // ★ 泛型参数 <T, U, ...>
            if (check(TokenType::Less)){
                advance();      // <
                cls += "<";
                cls += parseTypeName();
                while (match(TokenType::Comma)){
                    cls += ", " + parseTypeName();
                }
                eatGreater();
                cls += ">";
            }
            
            eat(TokenType::LParen);
            std::vector<AstNodePtr> args;
            if (!check(TokenType::RParen)){
                do { args.push_back(parseArgument()); } while (match(TokenType::Comma));
            }
            eat(TokenType::RParen);
            return std::make_shared<NewNode>(cls, args, token.line);
        }
        if (token.type == TokenType::KwTypeof){
            advance();
            eat(TokenType::LParen);
            AstNodePtr inner = parseExpression();
            eat(TokenType::RParen);
            return std::make_shared<TypeofNode>(inner);
        }
        if (token.type == TokenType::True){
            advance(); return std::make_shared<BoolNode>(true);
        }
        if (token.type == TokenType::False){
            advance(); return std::make_shared<BoolNode>(false);
        }
        if (token.type == TokenType::Null){
            advance();
            return std::make_shared<NullNode>();
        }
        if (token.type == TokenType::StringLiteral){
            advance(); return std::make_shared<StringNode>(token.value);
        }
        if (token.type == TokenType::CharLiteral){
            advance();
            if (token.value.empty()) raise(EX_VALUE, "Empty char literal", token.line);
            return std::make_shared<CharNode>(token.value[0]);
        }

        // ---- 类型转换 / lambda ----
        if (isTypeKeyword(token.type)){
            // ★ 先向前扫描，判断是否 lambda 语法：类型名( ... ) =>
            std::size_t look = _pos + 1;
            if (look < _tokens.size() && _tokens[look].type == TokenType::LParen){
                int depth = 0;
                std::size_t i = look;
                while (i < _tokens.size()){
                    if (_tokens[i].type == TokenType::LParen) depth++;
                    else if (_tokens[i].type == TokenType::RParen){
                        depth--;
                        if (depth == 0) break;
                    }
                    i++;
                }
                if (i + 1 < _tokens.size() && _tokens[i + 1].type == TokenType::Arrow){
                    return parseLambda();   // ← 是 lambda
                }
            }

            // 否则走类型转换
            std::string typeName = token.value;
            advance();
            eat(TokenType::LParen);
            AstNodePtr inner = parseExpression();
            AstNodePtr extra = nullptr;
            if (match(TokenType::Comma)){
                extra = parseExpression();
            }
            eat(TokenType::RParen);
            return std::make_shared<CastNode>(typeName, inner, extra, token.line);
        }

        if (token.type == TokenType::LBracket){
            advance();
            std::vector<AstNodePtr> items;
            if (!check(TokenType::RBracket)){
                do { items.push_back(parseExpression()); } while (match(TokenType::Comma));
            }
            eat(TokenType::RBracket);
            return std::make_shared<ListLiteralNode>(items);
        }

        if (token.type == TokenType::LBrace){
            advance();
            std::vector<AstNodePtr> keys, values;
            if (!check(TokenType::RBrace)){
                do {
                    keys.push_back(parseExpression());
                    eat(TokenType::Colon);
                    values.push_back(parseExpression());
                } while (match(TokenType::Comma));
            }
            eat(TokenType::RBrace);
            return std::make_shared<DictLiteralNode>(keys, values);
        }

        if (token.type == TokenType::LParen){
            std::size_t i = 1;
            if (_pos + i < _tokens.size() && isTypeKeyword(_tokens[_pos + i].type)){
                if (_pos + i + 1 < _tokens.size() &&
                    _tokens[_pos + i + 1].type == TokenType::RParen){
                    int ln = current().line;
                    advance();
                    std::string targetType = advance().value;
                    eat(TokenType::RParen);
                    AstNodePtr expr = parsePostfix(parsePrimary());
                    return std::make_shared<CastNode>(targetType, expr, nullptr, ln);
                }
            }

            advance();   // 吃 (

            // ★ 尝试 assign-expr: (a = 1 ; a) / (a = 1 ; b = 2 ; c)
            if (auto ae = tryParseAssignExpr()){
                eat(TokenType::RParen);
                return ae;
            }

            AstNodePtr first = parseExpression();
            if (match(TokenType::Comma)){
                std::vector<AstNodePtr> items { first };
                items.push_back(parseExpression());
                while (match(TokenType::Comma)){
                    items.push_back(parseExpression());
                }
                eat(TokenType::RParen);
                return std::make_shared<ArrayLiteralNode>(items);
            }
            eat(TokenType::RParen);
            return first;
        }

        raise(EX_SYNTAX,
              "Expected expression, but encountered " + tokenTypeName(token.type),
              token.line);
        return nullptr;
    }

    AstNodePtr parseDeclarationBody(bool isConst){
        std::string typeName = parseTypeName();

        if (!check(TokenType::Identifier)){
            raise(EX_SYNTAX, "Expected identifier after type", current().line);
        }
        Token nameTok = advance();
        std::vector<std::string> names { nameTok.value };
        std::vector<AstNodePtr> values;

        // ★ 首变量约束
        if (match(TokenType::ArrowRight)){
            ConstraintSet c = parseConstraint();
            registerConstraint(nameTok.value, c);
        }

        if (match(TokenType::Equal)){
            values.push_back(parseExpression());
        } else {
            values.push_back(defaultValue(typeName));
        }
        while (match(TokenType::Comma)){
            Token n = advance();
            names.push_back(n.value);

            // ★ 后续变量约束
            if (match(TokenType::ArrowRight)){
                ConstraintSet c = parseConstraint();
                registerConstraint(n.value, c);
            }

            if (match(TokenType::Equal)){
                values.push_back(parseExpression());
            } else {
                values.push_back(defaultValue(typeName));
            }
        }
        return std::make_shared<VariableDeclNode>(typeName, names, values, isConst, nameTok.line);
    }

    AstNodePtr parseSimpleStatement(){
        int srcLine = current().line;
        AstNodePtr expr = parseExpression();

        // x--
        if (match(TokenType::SubSub)){
            AstNodePtr sum = std::make_shared<BinaryNode>(expr, TokenType::Sub,
                std::make_shared<NumberNode>(1), srcLine);
            if (auto v = std::dynamic_pointer_cast<VariableNode>(expr)){
                return std::make_shared<AssignmentNode>(v->name, sum, srcLine);
            }
            return std::make_shared<AssignmentNode>(expr, sum, srcLine);
        }

        // x = ...
        if (match(TokenType::Equal)){
            AstNodePtr v = parseExpression();
            if (auto var = std::dynamic_pointer_cast<VariableNode>(expr)){
                return std::make_shared<AssignmentNode>(var->name, v, srcLine);
            }
            return std::make_shared<AssignmentNode>(expr, v, srcLine);
        }

        // x += ...
        if (match(TokenType::PlusEqual)){
            AstNodePtr v = parseExpression();
            std::shared_ptr<AssignmentNode> node;
            if (auto var = std::dynamic_pointer_cast<VariableNode>(expr)){
                node = std::make_shared<AssignmentNode>(var->name, v, srcLine);
            } else {
                node = std::make_shared<AssignmentNode>(expr, v, srcLine);
            }
            node->isCompound = true;
            node->compoundOp = "+=";
            return node;
        }

        // x -= ...
        if (match(TokenType::SubEqual)){
            AstNodePtr v = parseExpression();
            std::shared_ptr<AssignmentNode> node;
            if (auto var = std::dynamic_pointer_cast<VariableNode>(expr)){
                node = std::make_shared<AssignmentNode>(var->name, v, srcLine);
            } else {
                node = std::make_shared<AssignmentNode>(expr, v, srcLine);
            }
            node->isCompound = true;
            node->compoundOp = "-=";
            return node;
        }

        // ★ x *= ...
        if (match(TokenType::MulEqual)){
            AstNodePtr v = parseExpression();
            std::shared_ptr<AssignmentNode> node;
            if (auto var = std::dynamic_pointer_cast<VariableNode>(expr)){
                node = std::make_shared<AssignmentNode>(var->name, v, srcLine);
            } else {
                node = std::make_shared<AssignmentNode>(expr, v, srcLine);
            }
            node->isCompound = true;
            node->compoundOp = "*=";
            return node;
        }

        // ★ x /= ...
        if (match(TokenType::DivEqual)){
            AstNodePtr v = parseExpression();
            std::shared_ptr<AssignmentNode> node;
            if (auto var = std::dynamic_pointer_cast<VariableNode>(expr)){
                node = std::make_shared<AssignmentNode>(var->name, v, srcLine);
            } else {
                node = std::make_shared<AssignmentNode>(expr, v, srcLine);
            }
            node->isCompound = true;
            node->compoundOp = "/=";
            return node;
        }

        // ★ x %= ...
        if (match(TokenType::ModEqual)){
            AstNodePtr v = parseExpression();
            std::shared_ptr<AssignmentNode> node;
            if (auto var = std::dynamic_pointer_cast<VariableNode>(expr)){
                node = std::make_shared<AssignmentNode>(var->name, v, srcLine);
            } else {
                node = std::make_shared<AssignmentNode>(expr, v, srcLine);
            }
            node->isCompound = true;
            node->compoundOp = "%=";
            return node;
        }

        return std::make_shared<ExpressionStatementNode>(expr);
    }

    // ---------------- 表达式语句 ----------------
    AstNodePtr parseExpressionStatement(){
        int line = current().line;
        AstNodePtr expr = parseExpression();

        if (match(TokenType::SubSub)){
            eat(TokenType::Semicolon);
            AstNodePtr sum = std::make_shared<BinaryNode>(expr, TokenType::Sub,
                std::make_shared<NumberNode>(1), line);
            if (auto v = std::dynamic_pointer_cast<VariableNode>(expr)){
                return makeAssign(v->name, sum, line);
            }
            return std::make_shared<AssignmentNode>(expr, sum, line);
        }
        if (match(TokenType::Equal)){
            AstNodePtr value = parseExpression();
            eat(TokenType::Semicolon);
            if (auto v = std::dynamic_pointer_cast<VariableNode>(expr)){
                return makeAssign(v->name, value, line);
            }
            return std::make_shared<AssignmentNode>(expr, value, line);
        }
        if (match(TokenType::PlusEqual)){
            AstNodePtr value = parseExpression();
            eat(TokenType::Semicolon);
            std::shared_ptr<AssignmentNode> node;
            if (auto v = std::dynamic_pointer_cast<VariableNode>(expr)){
                node = makeAssign(v->name, value, line);
            } else {
                node = std::make_shared<AssignmentNode>(expr, value, line);
            }
            node->isCompound = true;
            node->compoundOp = "+=";
            return node;
        }
        if (match(TokenType::SubEqual)){
            AstNodePtr value = parseExpression();
            eat(TokenType::Semicolon);
            std::shared_ptr<AssignmentNode> node;
            if (auto v = std::dynamic_pointer_cast<VariableNode>(expr)){
                node = makeAssign(v->name, value, line);
            } else {
                node = std::make_shared<AssignmentNode>(expr, value, line);
            }
            node->isCompound = true;
            node->compoundOp = "-=";
            return node;
        }
        // ★ *= /= %=
        if (match(TokenType::MulEqual)){
            AstNodePtr value = parseExpression();
            eat(TokenType::Semicolon);
            std::shared_ptr<AssignmentNode> node;
            if (auto v = std::dynamic_pointer_cast<VariableNode>(expr)){
                node = makeAssign(v->name, value, line);
            } else {
                node = std::make_shared<AssignmentNode>(expr, value, line);
            }
            node->isCompound = true;
            node->compoundOp = "*=";
            return node;
        }
        if (match(TokenType::DivEqual)){
            AstNodePtr value = parseExpression();
            eat(TokenType::Semicolon);
            std::shared_ptr<AssignmentNode> node;
            if (auto v = std::dynamic_pointer_cast<VariableNode>(expr)){
                node = makeAssign(v->name, value, line);
            } else {
                node = std::make_shared<AssignmentNode>(expr, value, line);
            }
            node->isCompound = true;
            node->compoundOp = "/=";
            return node;
        }
        if (match(TokenType::ModEqual)){
            AstNodePtr value = parseExpression();
            eat(TokenType::Semicolon);
            std::shared_ptr<AssignmentNode> node;
            if (auto v = std::dynamic_pointer_cast<VariableNode>(expr)){
                node = makeAssign(v->name, value, line);
            } else {
                node = std::make_shared<AssignmentNode>(expr, value, line);
            }
            node->isCompound = true;
            node->compoundOp = "%=";
            return node;
        }
        if (match(TokenType::Semicolon)){
            auto node = std::make_shared<ExpressionStatementNode>(expr);
            if (std::dynamic_pointer_cast<CallNode>(expr)){
                std::string srcLine = getLine(line);
                int startCol = 0;
                while (startCol < (int)srcLine.size() &&
                       (srcLine[startCol] == ' ' || srcLine[startCol] == '\t')){
                    startCol++;
                }
                int semiPos = (int)srcLine.find(';', startCol);
                if (semiPos < 0) semiPos = (int)srcLine.size();

                node->hasTrace = true;
                node->traceFile = _fileName;
                node->traceSrcLine = srcLine;
                node->traceLine = line;
                node->traceColStart = startCol;
                node->traceColLen = semiPos - startCol;
            }
            return node;
        }

        std::string hint;
        int caretCol = -1;

        if (current().type == TokenType::END ||
            current().type == TokenType::RBrace ||
            current().type == TokenType::Using ||
            current().type == TokenType::KwClass ||
            current().type == TokenType::KwStruct ||
            current().type == TokenType::KwEnum){
            hint = "did you forget a ';' at the end of the previous line?";
            caretCol = _lastToken.col + (int)_lastToken.value.size();
        }

        if (caretCol >= 0){
            raise(EX_SYNTAX,
                  "Expected '=' or ';' after expression, but encountered "
                  + tokenTypeName(current().type) + "\n  (hint: " + hint + ")",
                  _lastToken.line, caretCol, "expected ';' here");
        } else {
            raise(EX_SYNTAX,
                  "Expected '=' or ';' after expression, but encountered "
                  + tokenTypeName(current().type),
                  _lastToken.line);
        }
    }

    // ★ 解析 template <typename T, typename U> 前缀
    std::vector<std::string> parseTemplatePrefix(){
        std::vector<std::string> params;
        if (!check(TokenType::KwTemplate)) return params;

        advance();                      // template
        eat(TokenType::Less);           // <

        while (true){
            if (!check(TokenType::KwTypename)){
                raise(EX_SYNTAX,
                      "Expected 'typename' in template parameter list",
                      current().line);
            }
            advance();

            if (!check(TokenType::Identifier)){
                raise(EX_SYNTAX,
                      "Expected template parameter name",
                      current().line);
            }
            params.push_back(advance().value);

            if (!match(TokenType::Comma)) break;
        }
        eatGreater();                   // > 或 >> 拆分
        return params;
    }

    // ---------------- 顶层声明 ----------------
    AstNodePtr parseDeclaration(){
        bool isConst = match(TokenType::KwConst);
        std::string typeName = parseTypeName();

        if (!check(TokenType::Identifier)){
            raise(EX_SYNTAX, "Expected identifier after type '" + typeName + "'", current().line);
        }
        Token nameTok = advance();
        std::string name = nameTok.value;

        if (check(TokenType::LParen)){
            if (isConst) raise(EX_TYPE, "Function cannot be const", nameTok.line);
            return parseFunctionDeclaration(typeName, name);
        }
        return parseVariableDeclaration(typeName, name, isConst);
    }

    // ============ 限定表达式支持 ============

    ConstraintSet parseConstraint(){
        ConstraintSet set;

        // 循环解析：A[args] & B[args] & ...
        while (true){
            if (!check(TokenType::Identifier)){
                raise(EX_SYNTAX, "Expected constraint class name after '->'", current().line);
            }
            std::string cls = advance().value;

            // 展开 namespace 别名 (edit → vox_mod_Code_edit)
            auto it = _namespaceAliases.find(cls);
            if (it != _namespaceAliases.end()) cls = it->second;

            // 支持 a.b.C 形式
            while (check(TokenType::Dot) && peek(1).type == TokenType::Identifier){
                advance();
                Token seg = advance();
                cls += "::" + seg.value;
            }

            // 若还是裸类名，尝试补 namespace 前缀
            if (cls.find("::") == std::string::npos){
                auto cit = _classToNamespace.find(cls);
                if (cit != _classToNamespace.end() && !cit->second.empty()){
                    cls = cit->second + "::" + cls;
                }
            }

            // ★ [args] 可选
            std::vector<AstNodePtr> args;
            if (match(TokenType::LBracket)){
                if (!check(TokenType::RBracket)){
                    do {
                        args.push_back(parseExpression());
                    } while (match(TokenType::Comma));
                }
                eat(TokenType::RBracket);
            }

            Constraint c;
            c.className = cls;
            c.args = std::move(args);
            set.items.push_back(std::move(c));

            // ★ 下一个约束用 & 连接
            if (!match(TokenType::Amp)) break;
        }
        return set;
    }

    void registerConstraint(const std::string& name, const ConstraintSet& c){
        if (!_constraintStack.empty()){
            _constraintStack.back()[name] = c;
        }
    }

    std::optional<ConstraintSet> findConstraint(const std::string& name) const {
        for (auto it = _constraintStack.rbegin(); it != _constraintStack.rend(); ++it){
            auto f = it->find(name);
            if (f != it->end()) return f->second;
        }
        return std::nullopt;
    }

    std::shared_ptr<AssignmentNode> makeAssign(const std::string& name,
                                                AstNodePtr value, int line){
        auto node = std::make_shared<AssignmentNode>(name, value, line);
        auto c = findConstraint(name);
        if (c) node->constraint = c;
        return node;
    }

    AstNodePtr parseVariableDeclaration(const std::string& typeName,
                                        const std::string& firstName,
                                        bool isConst){
        std::vector<std::string> names { firstName };
        std::vector<AstNodePtr> values;

        // ★ 首变量的约束
        if (match(TokenType::ArrowRight)){
            ConstraintSet c = parseConstraint();
            registerConstraint(firstName, c);
        }

        if (match(TokenType::Equal)){
            values.push_back(parseExpression());
        } else {
            values.push_back(defaultValue(typeName));
        }
        while (match(TokenType::Comma)){
            if (!check(TokenType::Identifier)){
                raise(EX_SYNTAX, "Expected variable name after ','", current().line);
            }
            Token n = advance();
            names.push_back(n.value);

            // ★ 后续变量也可以带约束
            if (match(TokenType::ArrowRight)){
                ConstraintSet c = parseConstraint();
                registerConstraint(n.value, c);
            }

            if (match(TokenType::Equal)){
                values.push_back(parseExpression());
            } else {
                values.push_back(defaultValue(typeName));
            }
        }
        if (!check(TokenType::Semicolon)){
            // 友好提示
            raise(EX_SYNTAX,
                  "Expected ';' after variable declaration, but encountered "
                  + tokenTypeName(current().type)
                  + "\n  (hint: did you forget a ';' at the end of the previous line?)",
                  current().line);
        }
        advance();
        return std::make_shared<VariableDeclNode>(typeName, names, values, isConst, _lastToken.line);
    }

    // ==================== ref lambda 后处理 ====================

    // 收集"函数体里的局部变量名"，跳过嵌套 lambda 内部
    void collectLocalVarNames(const AstNodePtr& node, std::set<std::string>& out){
        if (!node) return;

        // 遇到 LambdaNode：跳过（它内部是独立作用域）
        if (std::dynamic_pointer_cast<LambdaNode>(node)) return;

        if (auto var = std::dynamic_pointer_cast<VariableDeclNode>(node)){
            for (const auto& n : var->names) out.insert(n);
        }

        for (const auto& c : getChildren(node)){
            collectLocalVarNames(c, out);
        }
    }

    // 收集 ref lambda 里出现的所有标识符名，跳过嵌套 lambda 内部
    void collectUsedNames(const AstNodePtr& node, std::set<std::string>& out){
        if (!node) return;

        if (std::dynamic_pointer_cast<LambdaNode>(node)) return;   // 跳过嵌套

        if (auto v = std::dynamic_pointer_cast<VariableNode>(node)){
            out.insert(v->name);
            return;
        }

        for (const auto& c : getChildren(node)){
            collectUsedNames(c, out);
        }
    }

    // 收集所有 ref lambda 的指针
    void collectRefLambdas(const AstNodePtr& node,
                           std::vector<LambdaNode*>& out){
        if (!node) return;

        if (auto lam = std::dynamic_pointer_cast<LambdaNode>(node)){
            if (lam->byRef){
                out.push_back(lam.get());
            }
            // 嵌套 lambda 也要看（它内部也可能是 ref lambda）
        }

        for (const auto& c : getChildren(node)){
            collectRefLambdas(c, out);
        }
    }

    // 标记：body 里名字在 names 集合里的局部变量，设为 isHeapCaptured
    void markHeapCaptured(const AstNodePtr& node,
                          const std::set<std::string>& names){
        if (!node) return;

        if (std::dynamic_pointer_cast<LambdaNode>(node)) return;   // 跳过 lambda 内部

        if (auto var = std::dynamic_pointer_cast<VariableDeclNode>(node)){
            for (const auto& n : var->names){
                if (names.count(n)){
                    var->isHeapCaptured = true;
                    break;
                }
            }
        }

        for (const auto& c : getChildren(node)){
            markHeapCaptured(c, names);
        }
    }

    // ★ 主后处理：对函数/方法体处理 ref lambda
    void processRefCapturesInBody(const std::shared_ptr<BlockNode>& body){
        if (!body) return;

        // 1. 收集所有 ref lambda
        std::vector<LambdaNode*> refLambdas;
        collectRefLambdas(body, refLambdas);
        if (refLambdas.empty()) return;

        // 2. 收集函数体局部变量名（不含 lambda 内部）
        std::set<std::string> localVars;
        collectLocalVarNames(body, localVars);

        // 3. 对每个 ref lambda，收集它用到的名字
        std::set<std::string> heapVarNames;
        for (auto* lam : refLambdas){
            std::set<std::string> used;
            collectUsedNames(lam->body, used);

            // 只挑"函数体局部变量"里的名字（排除 lambda 自身参数、全局变量）
            for (const auto& name : used){
                // 排除 lambda 参数
                bool isParam = false;
                for (const auto& p : lam->params){
                    if (p.name == name) { isParam = true; break; }
                }
                if (isParam) continue;
            }
        }

        // 4. 标记所有需要堆分配的局部变量
        markHeapCaptured(body, heapVarNames);
    }

    AstNodePtr parseFunctionDeclaration(const std::string& returnType, const std::string& name){
        int startLine = _lastToken.line;
        auto params = parseParamList();
        if (match(TokenType::Semicolon)){
            return std::make_shared<FunctionDeclNode>(
                returnType, name, params, nullptr, false, startLine);
        }

        // ★ 顶层自由函数不能是 const
        if (check(TokenType::KwConst)){
            raise(EX_TYPE,
                "'" + name + "' is a free function; "
                "only class methods can be declared const",
                current().line);
        }

        eat(TokenType::LBrace);

        // ★ 参数作用域层
        _scopeVarStack.push_back({});
        for (const auto& p : params){
            _scopeVarStack.back().push_back(p.name);
        }

        auto body = std::make_shared<BlockNode>();
        parseBlockInto(body);
        eat(TokenType::RBrace);

        _scopeVarStack.pop_back();                        // ← 弹参数层

        return std::make_shared<FunctionDeclNode>(returnType, name, params, body, false, startLine);
    }


    // ---------------- 赋值表达式前瞻 ----------------
    bool looksLikeAssignmentPrefix() const {
        if (!check(TokenType::Identifier)) return false;
        std::size_t i = 1;
        while (_pos + i < _tokens.size()){
            TokenType t = _tokens[_pos + i].type;
            if (t == TokenType::LBracket){
                int depth = 1;
                i++;
                while (_pos + i < _tokens.size() && depth > 0){
                    TokenType tt = _tokens[_pos + i].type;
                    if (tt == TokenType::LBracket) depth++;
                    else if (tt == TokenType::RBracket) depth--;
                    i++;
                }
            } else if (t == TokenType::Dot){
                i++;
                if (_pos + i < _tokens.size() &&
                    _tokens[_pos + i].type == TokenType::Identifier){
                    i++;
                } else {
                    break;
                }
            } else {
                break;
            }
        }
        if (_pos + i >= _tokens.size()) return false;
        TokenType t = _tokens[_pos + i].type;

        // 复合赋值
        if (t == TokenType::Equal
            || t == TokenType::PlusEqual
            || t == TokenType::SubEqual
            || t == TokenType::MulEqual
            || t == TokenType::DivEqual
            || t == TokenType::ModEqual){
            return true;
        }

        // ★ i++ ; / i-- ;   —— 只在 i++ 后面紧跟 ';' 时才算
        if (t == TokenType::PlusPlus || t == TokenType::SubSub){
            return (i + 1 < _tokens.size()
                    && _tokens[_pos + i + 1].type == TokenType::Semicolon);
        }
        return false;
    }

    // 尝试解析 assign-expr:  a = 1 ; b = 2 ; c
    // 失败返回 nullptr（_pos 恢复原状）
    // 成功时 _pos 停在 finalExpr 之后（`;` 已消费完，等待 `)`）
    AstNodePtr tryParseAssignExpr(){
        if (!looksLikeAssignmentPrefix()) return nullptr;

        std::size_t save = _pos;
        std::vector<AssignPair> assigns;

        while (true){
            if (!looksLikeAssignmentPrefix()) break;

            int assignLine = current().line;

            AstNodePtr lhs = parsePostfix(parsePrimary());

            std::string opStr;
            AstNodePtr rhs;

            // ★ parsePostfix 可能已经吃掉了 i++ / i--
            if (auto pi = std::dynamic_pointer_cast<PostIncrementNode>(lhs)){
                opStr = pi->isDecrement ? "-=" : "+=";
                rhs = std::make_shared<NumberNode>(1, "1");
                lhs = pi->target;   // 用 target 作为左值
            }
            else if (check(TokenType::Equal)){          opStr = "=";  advance();
                rhs = parseExpression(); }
            else if (check(TokenType::PlusEqual)){      opStr = "+="; advance();
                rhs = parseExpression(); }
            else if (check(TokenType::SubEqual)){       opStr = "-="; advance();
                rhs = parseExpression(); }
            else if (check(TokenType::MulEqual)){       opStr = "*="; advance();
                rhs = parseExpression(); }
            else if (check(TokenType::DivEqual)){       opStr = "/="; advance();
                rhs = parseExpression(); }
            else if (check(TokenType::ModEqual)){       opStr = "%="; advance();
                rhs = parseExpression(); }
            else {
                if (assigns.empty()){ _pos = save; return nullptr; }
                raise(EX_SYNTAX,
                      "Expected '=', '+=', '-=', '*=', '/=', or '%=' "
                      "after assignment target",
                      current().line);
            }

            AssignPair p;
            p.target = lhs;
            p.op = opStr;
            p.value = rhs;
            p.line = assignLine;
            assigns.push_back(p);

            if (!match(TokenType::Semicolon)){
                if (assigns.size() == 1){
                    _pos = save;
                    return nullptr;
                }
                raise(EX_SYNTAX,
                      "Expected ';' after assignment in assign-expression",
                      current().line);
            }
        }

        AstNodePtr finalExpr = parseExpression();
        return std::make_shared<AssignExprNode>(assigns, finalExpr);
    }

    AstNodePtr parseArgument(){
        if (check(TokenType::KwRef)){
            advance();
            return parseExpression();
        }

        // ★ 先尝试 assign-expr:  a = 1 ; b = 2 ; c
        if (auto ae = tryParseAssignExpr()){
            return ae;
        }

        // 再尝试命名参数:  name = value
        if (looksLikeAssignmentPrefix()){
            std::size_t save = _pos;
            AstNodePtr lhs = parsePostfix(parsePrimary());
            if (check(TokenType::Equal)){
                advance();
                AstNodePtr rhs = parseExpression();
                if (auto v = std::dynamic_pointer_cast<VariableNode>(lhs)){
                    return std::make_shared<NamedArgNode>(v->name, rhs);
                }
            }
            _pos = save;
        }
        return parseExpression();
    }

    // 解析一串 @name 或 @name(arg1, arg2, ...)
    // 调用前 current() 是 At
    std::vector<Attribute> parseAttributes(){
        std::vector<Attribute> attrs;
        while (check(TokenType::At)){
            int attrLine = current().line;
            advance();                               // 吃掉 '@'

            if (!check(TokenType::Identifier)){
                raise(EX_SYNTAX, "Expected attribute name after '@'", attrLine);
            }
            Token nameTok = advance();
            Attribute attr;
            attr.name = nameTok.value;
            attr.line = attrLine;

            // 可选参数列表
            if (match(TokenType::LParen)){
                if (!check(TokenType::RParen)){
                    do {
                        // 参数：字符串 / 数字 / 标识符（类型名）
                        Token t = current();
                        if (t.type == TokenType::StringLiteral){
                            advance();
                            attr.args.push_back(t.value);        // 去引号
                        } else if (t.type == TokenType::Number){
                            advance();
                            attr.args.push_back(t.value);
                        } else if (t.type == TokenType::Identifier){
                            advance();
                            attr.args.push_back(t.value);
                        } else {
                            raise(EX_SYNTAX,
                                  "Expected attribute argument (string/number/identifier), "
                                  "but encountered " + tokenTypeName(t.type),
                                  t.line);
                        }
                    } while (match(TokenType::Comma));
                }
                eat(TokenType::RParen);
            }

            attrs.push_back(std::move(attr));
        }
        return attrs;
    }

    // ★ 增量收集：把 program 里尚未收集的顶层语句加进符号表
    void collectNewSignatures(const std::shared_ptr<ProgramNode>& program){
        while (_collectedStmtCount < program->statements.size()){
            collectSignatures(program->statements[_collectedStmtCount]);
            collectGlobalVars(program->statements[_collectedStmtCount]);
            _collectedStmtCount++;
        }
    }

    // ---------- 收集函数签名 ----------
    void collectSignatures(AstNodePtr node){
        if (!node) return;

        if (auto fn = std::dynamic_pointer_cast<FunctionDeclNode>(node)){
            if (fn->templateParams.empty()){
                _functionSignatures[fn->name].push_back(fn->params);
            } else {
                _functionSignatures[fn->name];
                _genericFunctions.insert(fn->name);   // ★ 记入泛型函数表
            }
            if (fn->body) collectSignatures(fn->body);
        } else if (auto cls = std::dynamic_pointer_cast<ClassNode>(node)){
            if (cls->templateParams.empty()){
                for (const auto& m : cls->methods){
                    _functionSignatures[cls->name + "." + m.name].push_back(m.params);
                    if (m.isCtor){
                        _functionSignatures[cls->name].push_back(m.params);
                        // ★ 加这行
                        _functionSignatures[cls->name + "._init_"].push_back(m.params);
                    }
                    if (m.isDtor){
                        // ★ dtor 也可能需要（但一般没参数，问题不大）
                    }
                }
            } else {
                // ★ 模板类：注册空签名（类名 + 各方法名）
                _functionSignatures[cls->name];
                for (const auto& m : cls->methods){
                    _functionSignatures[cls->name + "." + m.name];
                }
            }
            for (const auto& m : cls->methods){
                if (m.name == "_bool_" && !m.isStatic && !m.isCtor && m.params.empty()){
                    _classesWithBoolOp.insert(cls->name);
                }
            }
            for (const auto& m : cls->methods){
                if (m.body) collectSignatures(m.body);
            }
            for (const auto& m : cls->methods){
                if (m.name == "_with_" && !m.isStatic && !m.isCtor){
                    _classesWithWith.insert(cls->name);
                }
            }
        } else if (auto eb = std::dynamic_pointer_cast<ExternBlockNode>(node)){
            for (const auto& d : eb->decls){
                if (auto fn = std::dynamic_pointer_cast<FunctionDeclNode>(d)){
                    if (fn->templateParams.empty()){
                        _functionSignatures[fn->name].push_back(fn->params);
                    } else {
                        _functionSignatures[fn->name];   // ★ 模板函数：注册空签名
                    }
                }
            }
        } else if (auto ns = std::dynamic_pointer_cast<NamespaceNode>(node)){
            for (const auto& s : ns->statements){
                collectSignatures(s);
            }
        } else if (auto blk = std::dynamic_pointer_cast<BlockNode>(node)){
            for (const auto& s : blk->statements) collectSignatures(s);
        } else if (auto lam = std::dynamic_pointer_cast<LambdaNode>(node)){
            if (lam->body) collectSignatures(lam->body);
        }
    }

    // 收集所有非 extern 的顶层自由函数名
    void collectVoxFunctionNames(AstNodePtr node){
        if (!node) return;

        if (auto fn = std::dynamic_pointer_cast<FunctionDeclNode>(node)){
            if (!fn->isExtern){
                _voxFunctionNames.insert(fn->name);
            }
            return;
        }
        if (auto ns = std::dynamic_pointer_cast<NamespaceNode>(node)){
            for (const auto& s : ns->statements){
                collectVoxFunctionNames(s);
            }
            return;
        }
    }

    // 改名：给所有 Vox 顶层自由函数（定义 + 调用点）加 vox_ 前缀
    void renameVoxFunctions(AstNodePtr node){
        if (!node) return;

        // 函数定义改名
        if (auto fn = std::dynamic_pointer_cast<FunctionDeclNode>(node)){
            if (!fn->isExtern && _voxFunctionNames.count(fn->name)){
                fn->name = "vox_" + fn->name;
            }
            // 继续递归 body（里面有调用点）
        }

        // 函数调用改名
        if (auto call = std::dynamic_pointer_cast<CallNode>(node)){
            if (auto v = std::dynamic_pointer_cast<VariableNode>(call->callee)){
                if (_voxFunctionNames.count(v->name)){
                    v->name = "vox_" + v->name;
                }
            }
        }

        // 递归所有子节点
        for (const auto& c : getChildren(node)){
            renameVoxFunctions(c);
        }
    }

    // ---------- 遍历子节点 ----------
    std::vector<AstNodePtr> getChildren(const AstNodePtr& node){
        std::vector<AstNodePtr> result;
        if (!node) return result;

        if (auto n = std::dynamic_pointer_cast<MemberAccessNode>(node)){
            if (n->object) result.push_back(n->object);
        } else if (auto n = std::dynamic_pointer_cast<IndexNode>(node)){
            if (n->object) result.push_back(n->object);
            if (n->index)  result.push_back(n->index);
        } else if (auto n = std::dynamic_pointer_cast<AssignExprNode>(node)){
            for (const auto& p : n->assigns){
                if (p.target) result.push_back(p.target);
                if (p.value)  result.push_back(p.value);
            }
            if (n->finalExpr) result.push_back(n->finalExpr);
        } else if (auto n = std::dynamic_pointer_cast<UnaryNode>(node)){
            if (n->operand) result.push_back(n->operand);
        } else if (auto n = std::dynamic_pointer_cast<PostIncrementNode>(node)){
            if (n->target) result.push_back(n->target);
        } else if (auto n = std::dynamic_pointer_cast<CallNode>(node)){
            if (n->callee) result.push_back(n->callee);
            for (const auto& a : n->args) if (a) result.push_back(a);
        } else if (auto n = std::dynamic_pointer_cast<IsNullNode>(node)){
            if (n->expr) result.push_back(n->expr);
        } else if (auto n = std::dynamic_pointer_cast<NewNode>(node)){
            for (const auto& a : n->args) if (a) result.push_back(a);
        } else if (auto n = std::dynamic_pointer_cast<CastNode>(node)){
            if (n->expr) result.push_back(n->expr);
            if (n->extraArg) result.push_back(n->extraArg);
        } else if (auto n = std::dynamic_pointer_cast<IsNode>(node)){
            if (n->expr) result.push_back(n->expr);
        } else if (auto n = std::dynamic_pointer_cast<TypeofNode>(node)){
            if (n->expr) result.push_back(n->expr);
        } else if (auto n = std::dynamic_pointer_cast<LambdaNode>(node)){ //?
            if (n->body) result.push_back(n->body);                       //?
        } else if (auto n = std::dynamic_pointer_cast<NestedFunctionNode>(node)){
            if (n->body) result.push_back(n->body);                       //?
        } else if (auto n = std::dynamic_pointer_cast<ExpressionStatementNode>(node)){
            if (n->expr) result.push_back(n->expr);
        } else if (auto n = std::dynamic_pointer_cast<BinaryNode>(node)){
            if (n->left)  result.push_back(n->left);
            if (n->right) result.push_back(n->right);
        } else if (auto n = std::dynamic_pointer_cast<VariableDeclNode>(node)){
            for (const auto& v : n->values) if (v) result.push_back(v);
        } else if (auto n = std::dynamic_pointer_cast<AssignmentNode>(node)){
            if (n->target) result.push_back(n->target);
            if (n->value)  result.push_back(n->value);
        } else if (auto n = std::dynamic_pointer_cast<ListLiteralNode>(node)){
            for (const auto& x : n->items) if (x) result.push_back(x);
        } else if (auto n = std::dynamic_pointer_cast<ArrayLiteralNode>(node)){
            for (const auto& x : n->items) if (x) result.push_back(x);
        } else if (auto n = std::dynamic_pointer_cast<DictLiteralNode>(node)){
            for (const auto& k : n->keys)   if (k) result.push_back(k);
            for (const auto& v : n->values) if (v) result.push_back(v);
        } else if (auto n = std::dynamic_pointer_cast<BlockNode>(node)){
            for (const auto& s : n->statements) if (s) result.push_back(s);
        } else if (auto n = std::dynamic_pointer_cast<TryNode>(node)){
            if (n->tryBody)     result.push_back(n->tryBody);
            for (const auto& c : n->catches){
                if (c.body) result.push_back(c.body);
            }
            if (n->finallyBody) result.push_back(n->finallyBody);
        } else if (auto n = std::dynamic_pointer_cast<SwitchNode>(node)){
            if (n->expr) result.push_back(n->expr);
            for (const auto& c : n->cases){
                if (c.value) result.push_back(c.value);
                if (c.body)  result.push_back(c.body);
            }
        } else if (auto n = std::dynamic_pointer_cast<WithNode>(node)){   // ★
            if (n->init) result.push_back(n->init);
            if (n->body) result.push_back(n->body);
        } else if (auto n = std::dynamic_pointer_cast<RaiseNode>(node)){
            if (n->message) result.push_back(n->message);
        } else if (std::dynamic_pointer_cast<BreakNode>(node)){
            // 叶子，无子节点
        } else if (std::dynamic_pointer_cast<ContinueNode>(node)){
            // 叶子
        }else if (auto n = std::dynamic_pointer_cast<ReturnNode>(node)){
            if (n->expression) result.push_back(n->expression);
        } else if (auto n = std::dynamic_pointer_cast<ForNode>(node)){
            if (n->init) result.push_back(n->init);
            if (n->cond) result.push_back(n->cond);
            if (n->step) result.push_back(n->step);
            if (n->body) result.push_back(n->body);
        } else if (auto n = std::dynamic_pointer_cast<WhileNode>(node)){
            if (n->condition) result.push_back(n->condition);
            if (n->body)      result.push_back(n->body);
        } else if (auto n = std::dynamic_pointer_cast<DoWhileNode>(node)){
            if (n->body)      result.push_back(n->body);
            if (n->condition) result.push_back(n->condition);
        } else if (auto n = std::dynamic_pointer_cast<ForeachNode>(node)){
            for (const auto& v : n->vars) if (v.iterable) result.push_back(v.iterable);
            if (n->body) result.push_back(n->body);
        } else if (auto n = std::dynamic_pointer_cast<IfNode>(node)){
            if (n->condition)  result.push_back(n->condition);
            if (n->thenBody)   result.push_back(n->thenBody);
            if (n->elseBranch) result.push_back(n->elseBranch);
        } else if (auto n = std::dynamic_pointer_cast<TernaryNode>(node)){
            if (n->condition) result.push_back(n->condition);
            if (n->thenExpr)  result.push_back(n->thenExpr);
            if (n->elseExpr)  result.push_back(n->elseExpr);
        } else if (auto n = std::dynamic_pointer_cast<FunctionDeclNode>(node)){
            for (const auto& p : n->params) if (p.defaultValue) result.push_back(p.defaultValue);
            if (n->body) result.push_back(n->body);
        } else if (auto n = std::dynamic_pointer_cast<ExternBlockNode>(node)){
            for (const auto& d : n->decls) if (d) result.push_back(d);
        } else if (auto n = std::dynamic_pointer_cast<NamespaceNode>(node)){
            for (const auto& s : n->statements) if (s) result.push_back(s);
        } else if (auto n = std::dynamic_pointer_cast<ClassNode>(node)){
            for (const auto& f : n->fields) if (f.init) result.push_back(f.init);
            for (const auto& m : n->methods){
                for (const auto& p : m.params) if (p.defaultValue) result.push_back(p.defaultValue);
                if (m.body) result.push_back(m.body);
            }
        } else if (auto n = std::dynamic_pointer_cast<ProgramNode>(node)){
            for (const auto& s : n->statements) if (s) result.push_back(s);
        } else if (auto n = std::dynamic_pointer_cast<NamedArgNode>(node)){
            if (n->value) result.push_back(n->value);
        }
        return result;
    }

    // ---------- 递归解析所有调用 ----------
    void resolveCalls(const AstNodePtr& node){
        if (!node) return;
        if (auto call = std::dynamic_pointer_cast<CallNode>(node)){
            if (auto v = std::dynamic_pointer_cast<VariableNode>(call->callee)){
                if (_functionSignatures.find(v->name) == _functionSignatures.end()){
                    call->userInvoke = true;
                }
            }
            resolveCallParams(*call);
        } else if (auto nn = std::dynamic_pointer_cast<NewNode>(node)){
            resolveNewParams(*nn);
        }
        for (const auto& child : getChildren(node)){
            resolveCalls(child);
        }
    }

    void resolveNewParams(NewNode& nn){
        // 从 className 里提取最后一段：vox_mod_Code_window::Font → Font
        std::string cls = nn.className;
        auto pos = cls.rfind("::");
        if (pos != std::string::npos) cls = cls.substr(pos + 2);

        const std::vector<std::vector<Param>>* sigsPtr = findSignatureList(cls);
        if (!sigsPtr) return;
        const auto& allSigs = *sigsPtr;

        // ★ 先统计：位置参数几个、有没有命名参数
        std::size_t posCount = 0;
        bool hasNamed = false;
        for (const auto& a : nn.args){
            if (std::dynamic_pointer_cast<NamedArgNode>(a)) hasNamed = true;
            else posCount++;
        }

        // ★ 选签名
        const std::vector<Param>* matched = nullptr;
        if (hasNamed){
            // 有命名参数：挑参数最多的签名（最完整）
            std::size_t maxSize = 0;
            for (const auto& sig : allSigs){
                if (sig.size() > maxSize){
                    maxSize = sig.size();
                    matched = &sig;
                }
            }
        } else {
            // 无命名参数：优先精确匹配，再退到 min/max 覆盖
            for (const auto& sig : allSigs){
                if (sig.size() == posCount){
                    matched = &sig;
                    break;
                }
            }
            if (!matched){
                for (const auto& sig : allSigs){
                    std::size_t sMin = 0, sMax = 0;
                    bool hasVar = false;
                    for (const auto& p : sig){
                        if (p.isVariadic){ hasVar = true; continue; }
                        sMax++;
                        if (!p.defaultValue) sMin++;
                    }
                    if (hasVar) sMax = SIZE_MAX;
                    if (posCount >= sMin && posCount <= sMax){
                        matched = &sig;
                        break;
                    }
                }
            }
        }
        if (!matched) return;
        const std::vector<Param>& params = *matched;

        // 无命名参数、无 variadic → 不用重排
        if (!hasNamed){
            bool hasVariadic = false;
            for (const auto& p : params){
                if (p.isVariadic){ hasVariadic = true; break; }
            }
            if (!hasVariadic) return;
        }

        std::map<std::string, AstNodePtr> named;
        std::vector<AstNodePtr> positional;
        for (const auto& a : nn.args){
            if (auto na = std::dynamic_pointer_cast<NamedArgNode>(a)){
                named[na->name] = na->value;
            } else {
                positional.push_back(a);
            }
        }

        std::vector<std::vector<AstNodePtr>> argLists(params.size());
        std::size_t posIdx = 0;

        for (std::size_t pi = 0; pi < params.size(); ++pi){
            const Param& p = params[pi];
            auto nit = named.find(p.name);
            bool hasNamedVal = (nit != named.end());

            if (p.isVariadic){
                while (posIdx < positional.size()){
                    argLists[pi].push_back(positional[posIdx++]);
                }
                continue;
            }

            if (p.isPosOnly){
                if (hasNamedVal){
                    raise(EX_ARGUMENT,
                        "'" + cls + "' parameter '" + p.name
                        + "' is positional-only (before '/')",
                        nn.line);
                }
                if (posIdx < positional.size()){
                    argLists[pi].push_back(positional[posIdx++]);
                } else if (p.defaultValue){
                    argLists[pi].push_back(p.defaultValue);
                } else {
                    raise(EX_ARGUMENT,
                        "'" + cls + "' missing required argument '" + p.name + "'",
                        nn.line);
                }
                continue;
            }

            if (p.isKwOnly){
                if (hasNamedVal){
                    argLists[pi].push_back(nit->second);
                } else if (p.defaultValue){
                    argLists[pi].push_back(p.defaultValue);
                } else {
                    raise(EX_ARGUMENT,
                        "'" + cls + "' missing required keyword-only argument '"
                        + p.name + "' (after '*')",
                        nn.line);
                }
                continue;
            }

            // positional-or-keyword
            if (hasNamedVal){
                argLists[pi].push_back(nit->second);
            } else if (posIdx < positional.size()){
                argLists[pi].push_back(positional[posIdx++]);
            } else if (p.defaultValue){
                argLists[pi].push_back(p.defaultValue);
            } else {
                raise(EX_ARGUMENT,
                    "'" + cls + "' missing required argument '" + p.name + "'",
                    nn.line);
            }
        }

        std::vector<AstNodePtr> finalArgs;
        for (std::size_t pi = 0; pi < params.size(); ++pi){
            if (params[pi].isVariadic) continue;
            for (const auto& a : argLists[pi]) finalArgs.push_back(a);
        }
        for (std::size_t pi = 0; pi < params.size(); ++pi){
            if (!params[pi].isVariadic) continue;
            for (const auto& a : argLists[pi]) finalArgs.push_back(a);
        }

        nn.args = finalArgs;
    }

    // ★ 处理 variadic 调用点的两个特例：
    //   1) 单个 List 字面量 → 展开成多个参数
    //   2) variadic 位置的 NullNode → 设置 explicitType（生成 `vox::Any()` 而非 `{}`）
    void processVariadicCalls(CallNode& call){
        std::string name;

        if (auto m = std::dynamic_pointer_cast<MemberAccessNode>(call.callee)){
            AstNodePtr obj = m->object;
            while (auto mm = std::dynamic_pointer_cast<MemberAccessNode>(obj)){
                obj = mm->object;
            }
            if (std::dynamic_pointer_cast<SelfNode>(obj)) return;   // self 推断不出，跳过
            std::string cls = inferReceiverClass(m->object);
            if (cls.empty()) return;
            name = cls + "." + m->member;
        } else {
            name = callCalleeName(call.callee);
            if (name.empty()) return;
        }

        const std::vector<std::vector<Param>>* sigsPtr = findSignatureList(name);
        if (!sigsPtr) return;
        const auto& allSigs = *sigsPtr;

        // 找一个 variadic 签名
        for (const auto& sig : allSigs){
            std::size_t varIdx = (std::size_t)-1;
            for (std::size_t i = 0; i < sig.size(); ++i){
                if (sig[i].isVariadic){ varIdx = i; break; }
            }
            if (varIdx == (std::size_t)-1) continue;

            // ★ 情况 1：签名只有 variadic，调用点只传一个 List 字面量 → 展开
            if (sig.size() == 1 && varIdx == 0 && call.args.size() == 1){
                if (std::dynamic_pointer_cast<ListLiteralNode>(call.args[0])){
                    call.expandSingleListAsVariadic = true;
                }
            }

            // ★ 情况 2：variadic 位置上的 NullNode → 设置 explicitType
            std::string varType = sig[varIdx].type;
            for (std::size_t j = varIdx; j < call.args.size(); ++j){
                if (auto nn = std::dynamic_pointer_cast<NullNode>(call.args[j])){
                    if (nn->explicitType.empty()){
                        nn->explicitType = varType;
                    }
                }
            }
            return;
        }
    }

    // 尝试多种方式在 _functionSignatures 里找签名表：
    //   1) 原名
    //   2) 去掉 namespace 前缀后的短名（"vox_mod_Code::Debug.log" → "Debug.log"）
    const std::vector<std::vector<Param>>* findSignatureList(const std::string& name) const {
        // 1) 精确匹配
        auto it = _functionSignatures.find(name);
        if (it != _functionSignatures.end()) return &it->second;

        // 2) 剥 C++ 命名空间 + 类前缀： "vox_mod_Code::Debug.log" → "Debug.log"
        auto dotPos = name.rfind('.');
        if (dotPos != std::string::npos){
            std::string cls = name.substr(0, dotPos);
            std::string mem = name.substr(dotPos + 1);

            auto nsPos = cls.rfind("::");
            if (nsPos != std::string::npos){
                std::string shortCls = cls.substr(nsPos + 2);
                auto it2 = _functionSignatures.find(shortCls + "." + mem);
                if (it2 != _functionSignatures.end()) return &it2->second;
            }

            // ★ 3) 剥 Vox 模块前缀： "vox_mod_Code_urllib.put" → "put"
            if (cls.compare(0, 8, "vox_mod_") == 0){
                auto it3 = _functionSignatures.find(mem);
                if (it3 != _functionSignatures.end()) return &it3->second;
            }
        }

        // 4) 剥自由函数命名空间前缀： "vox_mod_Code::foo" → "foo"
        {
            auto nsPos = name.rfind("::");
            if (nsPos != std::string::npos){
                std::string shortName = name.substr(nsPos + 2);
                auto it4 = _functionSignatures.find(shortName);
                if (it4 != _functionSignatures.end()) return &it4->second;
            }
        }

        return nullptr;
    }

    // ---------- 把命名参数重排为位置参数 ----------
    void resolveCallParams(CallNode& call){
        if (!call.templateArgs.empty()) return;
        std::string name = callCalleeName(call.callee);
        if (name.empty()) return;

        // ★ 统一走 findSignatureList（内部含模块前缀剥离等规则）
        const std::vector<std::vector<Param>>* sigsPtr = findSignatureList(name);
        if (!sigsPtr) return;
        const auto& allSigs = *sigsPtr;
        if (allSigs.empty()){ return; }

        // ★★ 关键改动：先统计位置参数个数、有无命名参数
        std::size_t posCount = 0;
        bool hasNamed = false;
        for (const auto& a : call.args){
            if (std::dynamic_pointer_cast<NamedArgNode>(a)) hasNamed = true;
            else posCount++;
        }

        // ★★ 签名匹配
        const std::vector<Param>* matched = nullptr;
        if (hasNamed){
            // 有命名参数：挑参数最多的签名（最完整）
            std::size_t maxSize = 0;
            for (const auto& sig : allSigs){
                if (sig.size() > maxSize){
                    maxSize = sig.size();
                    matched = &sig;
                }
            }
        } else {
            // 无命名参数：优先精确匹配参数个数
            for (const auto& sig : allSigs){
                if (sig.size() == posCount){
                    matched = &sig;
                    break;
                }
            }
            // 不行就退到 min/max 覆盖
            if (!matched){
                for (const auto& sig : allSigs){
                    std::size_t sMin = 0, sMax = 0;
                    bool hasVar = false;
                    for (const auto& p : sig){
                        if (p.isVariadic){ hasVar = true; continue; }
                        sMax++;
                        if (!p.defaultValue) sMin++;
                    }
                    if (hasVar) sMax = SIZE_MAX;
                    if (posCount >= sMin && posCount <= sMax){
                        matched = &sig;
                        break;
                    }
                }
            }
            // 还不行就退到 variadic
            if (!matched){
                for (const auto& sig : allSigs){
                    for (const auto& p : sig){
                        if (p.isVariadic){ matched = &sig; break; }
                    }
                    if (matched) break;
                }
            }
        }
        if (!matched){ return; }
        const std::vector<Param>& params = *matched;

        // 无命名参数、无 variadic → 不用重排
        std::size_t variadicIdx = (std::size_t)-1;
        bool allFixedHaveDefaults = true;
        for (std::size_t i = 0; i < params.size(); ++i){
            if (params[i].isVariadic){
                variadicIdx = i;
            } else if (!params[i].defaultValue){
                allFixedHaveDefaults = false;
            }
        }

        if (!hasNamed && variadicIdx == (std::size_t)-1){
            return;
        }

        // ★ 拆分命名参数 / 位置参数
        std::map<std::string, AstNodePtr> named;
        std::vector<AstNodePtr> positional;
        for (const auto& a : call.args){
            if (auto na = std::dynamic_pointer_cast<NamedArgNode>(a)){
                named[na->name] = na->value;
            } else {
                positional.push_back(a);
            }
        }

        // ★ Debug.log("x") 这种：所有固定参数都有默认值 + 无 named → 位置参数全给 variadic
        if (allFixedHaveDefaults && variadicIdx != (std::size_t)-1 && !hasNamed){
            std::vector<std::vector<AstNodePtr>> argLists(params.size());
            for (const auto& p : positional){
                argLists[variadicIdx].push_back(p);
            }
            for (std::size_t pi = 0; pi < params.size(); ++pi){
                if (pi == variadicIdx) continue;
                if (params[pi].defaultValue){
                    argLists[pi].push_back(params[pi].defaultValue);
                }
            }
            std::vector<AstNodePtr> finalArgs;
            for (std::size_t pi = 0; pi < params.size(); ++pi){
                if (params[pi].isVariadic) continue;
                for (const auto& a : argLists[pi]) finalArgs.push_back(a);
            }
            for (std::size_t pi = 0; pi < params.size(); ++pi){
                if (!params[pi].isVariadic) continue;
                for (const auto& a : argLists[pi]) finalArgs.push_back(a);
            }
            call.args = finalArgs;
            return;
        }

        // ★ 逐个位置填（命名 / 位置 / 默认值）
        std::vector<std::vector<AstNodePtr>> argLists(params.size());
        std::size_t posIdx = 0;

        for (std::size_t pi = 0; pi < params.size(); ++pi){
            const Param& p = params[pi];
            auto nit = named.find(p.name);
            bool hasNamedVal = (nit != named.end());

            if (p.isVariadic){
                while (posIdx < positional.size()){
                    argLists[pi].push_back(positional[posIdx++]);
                }
                continue;
            }
            if (p.isPosOnly){
                if (hasNamedVal){
                    raise(EX_ARGUMENT,
                        "'" + name + "' parameter '" + p.name
                        + "' is positional-only (before '/')",
                        call.line);
                }
                if (posIdx < positional.size()){
                    argLists[pi].push_back(positional[posIdx++]);
                } else if (p.defaultValue){
                    argLists[pi].push_back(p.defaultValue);
                } else {
                    raise(EX_ARGUMENT,
                        "'" + name + "' missing required argument '" + p.name + "'",
                        call.line);
                }
                continue;
            }
            if (p.isKwOnly){
                if (hasNamedVal){
                    argLists[pi].push_back(nit->second);
                } else if (p.defaultValue){
                    argLists[pi].push_back(p.defaultValue);
                } else {
                    raise(EX_ARGUMENT,
                        "'" + name + "' missing required keyword-only argument '"
                        + p.name + "' (after '*')",
                        call.line);
                }
                continue;
            }
            if (hasNamedVal){
                argLists[pi].push_back(nit->second);
            } else if (posIdx < positional.size()){
                argLists[pi].push_back(positional[posIdx++]);
            } else if (p.defaultValue){
                argLists[pi].push_back(p.defaultValue);
            } else {
                raise(EX_ARGUMENT,
                    "'" + name + "' missing required argument '" + p.name + "'",
                    call.line);
            }
        }

        std::vector<AstNodePtr> finalArgs;
        for (std::size_t pi = 0; pi < params.size(); ++pi){
            if (params[pi].isVariadic) continue;
            for (const auto& a : argLists[pi]) finalArgs.push_back(a);
        }
        for (std::size_t pi = 0; pi < params.size(); ++pi){
            if (!params[pi].isVariadic) continue;
            for (const auto& a : argLists[pi]) finalArgs.push_back(a);
        }
        call.args = finalArgs;
    }

    // ================== 轻量级类型检查 ==================

    std::string lookupVarType(const std::string& name) const {
        // ★ 剥内部前缀： "vox_g_screen1" → "screen1"
        std::string nm = name;
        static const std::string voxPrefix = "vox_g_";
        if (nm.size() > voxPrefix.size() &&
            nm.compare(0, voxPrefix.size(), voxPrefix) == 0){
            nm = nm.substr(voxPrefix.size());
        }

        for (auto it = _typeScopeStack.rbegin(); it != _typeScopeStack.rend(); ++it){
            auto f = it->find(nm);
            if (f != it->end()) return f->second;
        }
        return "?";
    }

    bool isConstVar(const std::string& name) const {
        std::string nm = name;
        static const std::string voxPrefix = "vox_g_";
        if (nm.size() > voxPrefix.size() &&
            nm.compare(0, voxPrefix.size(), voxPrefix) == 0){
            nm = nm.substr(voxPrefix.size());
        }

        for (auto it = _constScopeStack.rbegin(); it != _constScopeStack.rend(); ++it){
            if (it->count(nm)) return true;
        }
        return false;
    }

    static bool isIntegerType(const std::string& t){
        return t == "int"
            || t == "int8"  || t == "int16"  || t == "int32"  || t == "int64"
            || t == "uint8" || t == "uint16" || t == "uint32" || t == "uint64";
    }
    static int integerRank(const std::string& t){
        if (t == "uint64") return 9;
        if (t == "int64")  return 8;
        if (t == "uint32") return 7;
        if (t == "int32")  return 6;
        if (t == "uint16") return 5;
        if (t == "int16")  return 4;
        if (t == "uint8")  return 3;
        if (t == "int8")   return 2;
        if (t == "int")    return 1;
        return 0;
    }
    static std::string widerInteger(const std::string& a, const std::string& b){
        return integerRank(a) >= integerRank(b) ? a : b;
    }

    std::string inferExprType(const AstNodePtr& node){
        if (!node) return "?";
        if (std::dynamic_pointer_cast<NumberNode>(node)) return "int";
        if (std::dynamic_pointer_cast<BoolNode>(node))   return "bool";
        if (std::dynamic_pointer_cast<FloatNode>(node))  return std::dynamic_pointer_cast<FloatNode>(node)->isFloat32 ? "float" : "double";
        if (std::dynamic_pointer_cast<StringNode>(node)) return "string";
        if (std::dynamic_pointer_cast<CharNode>(node))   return "char";
        if (std::dynamic_pointer_cast<TypeofNode>(node)) return "string";   // ★ typeof 永远返回 string
        if (auto n = std::dynamic_pointer_cast<LambdaNode>(node)){
        std::string sig = n->returnType + "(";
        for (std::size_t i = 0; i < n->params.size(); ++i){
            if (i > 0) sig += ", ";
                sig += n->params[i].type;
            }
            sig += ")";
            return sig;
        }
        if (auto n = std::dynamic_pointer_cast<VariableNode>(node))
            return lookupVarType(n->name);
        if (auto n = std::dynamic_pointer_cast<CastNode>(node))
            return n->targetType;
        if (auto n = std::dynamic_pointer_cast<NewNode>(node))
            return n->className;

        if (auto n = std::dynamic_pointer_cast<BinaryNode>(node)){
            switch (n->op){
            case TokenType::Less: case TokenType::Greater:
            case TokenType::LessEqual: case TokenType::GreaterEqual:
            case TokenType::EqualEqual: case TokenType::NotEqual:
            case TokenType::AmpAmp: case TokenType::PipePipe:
                return "bool";

            case TokenType::Plus: {
                std::string lt = inferExprType(n->left);
                std::string rt = inferExprType(n->right);
                if (lt == "string" || rt == "string") return "string";
                if (isNumericType(lt) && isNumericType(rt))
                    return widerNumeric(lt, rt);
                return "int";
            }
            case TokenType::Sub: case TokenType::Mul: case TokenType::Div:
            case TokenType::Mod:
            case TokenType::Shl: case TokenType::Shr:
            case TokenType::Amp: case TokenType::Pipe: case TokenType::Caret: {
                std::string lt = inferExprType(n->left);
                std::string rt = inferExprType(n->right);
                // 位运算只对整数有效，其它算术对浮点有效
                if (n->op == TokenType::Shl || n->op == TokenType::Shr ||
                    n->op == TokenType::Amp || n->op == TokenType::Pipe ||
                    n->op == TokenType::Caret){
                    if (isIntegerType(lt) && isIntegerType(rt))
                        return widerInteger(lt, rt);
                    return "int";
                }
                if (isNumericType(lt) && isNumericType(rt))
                    return widerNumeric(lt, rt);
                return "int";
            }
            default: return "?";
            }
        }

        if (auto n = std::dynamic_pointer_cast<UnaryNode>(node)){
            if (n->op == TokenType::Bang)  return "bool";
            if (n->op == TokenType::Tilde){
                std::string t = inferExprType(n->operand);
                if (isIntegerType(t)) return t;
                return "int";
            }
            if (auto n = std::dynamic_pointer_cast<PostIncrementNode>(node)){
                return inferExprType(n->target);   // i++ 的类型 = i 的类型
            }
            if (n->op == TokenType::Sub || n->op == TokenType::Plus){
                return inferExprType(n->operand);     // <-- 一元 +/- 返回操作数的类型
            }
            return inferExprType(n->operand);
        }

        if (auto n = std::dynamic_pointer_cast<IndexNode>(node)){
            std::string objType = inferExprType(n->object);
            if (objType.size() > 5 && objType.compare(0, 5, "List<") == 0 && objType.back() == '>')
                return objType.substr(5, objType.size() - 6);
            if (objType.size() > 10 && objType.compare(0, 10, "FinalList<") == 0 && objType.back() == '>')
                return objType.substr(10, objType.size() - 11);
            return "?";
        }
        if (auto n = std::dynamic_pointer_cast<ListLiteralNode>(node)){
            std::string inner = n->items.empty() ? "?" : inferExprType(n->items[0]);
            return "List<" + inner + ">";
        }
        if (std::dynamic_pointer_cast<DictLiteralNode>(node)) return "Dict<?,?>";
        return "?";
    }

    static bool isFloatType(const std::string& t){
        return t == "float" || t == "double";
    }
    static bool isNumericType(const std::string& t){
        return isIntegerType(t) || isFloatType(t);
    }
    static std::string widerNumeric(const std::string& a, const std::string& b){
        // 浮点优先
        if (a == "double" || b == "double") return "double";
        if (a == "float"  || b == "float")  return "float";
        return widerInteger(a, b);
    }

    // 取类型名的最后一段：vox_mod_Code_window::Window → Window
    static std::string lastTypeSegment(const std::string& t){
        auto pos = t.rfind("::");
        if (pos == std::string::npos) return t;
        return t.substr(pos + 2);
    }

    bool typeCompatible(const std::string& actual, const std::string& expected){
        if (actual == "?" || expected == "?") return true;
        if (actual == expected) return true;
        if (actual == "any" || expected == "any") return true;

        // ★ List<T> <-> FinalList<T>，Dict<K,V> <-> FinalDict<K,V> 兼容
        {
            auto parseContainer = [](const std::string& t,
                                     std::string& kind, std::string& inner) -> bool
            {
                if (t.compare(0, 10, "FinalList<") == 0 && t.back() == '>'){
                    kind = "List"; inner = t.substr(10, t.size() - 11); return true;
                }
                if (t.compare(0, 5, "List<") == 0 && t.back() == '>'){
                    kind = "List"; inner = t.substr(5, t.size() - 6); return true;
                }
                if (t.compare(0, 10, "FinalDict<") == 0 && t.back() == '>'){
                    kind = "Dict"; inner = t.substr(10, t.size() - 11); return true;
                }
                if (t.compare(0, 5, "Dict<") == 0 && t.back() == '>'){
                    kind = "Dict"; inner = t.substr(5, t.size() - 6); return true;
                }
                return false;
            };
            std::string aKind, aInner, eKind, eInner;
            bool aIsC = parseContainer(actual, aKind, aInner);
            bool eIsC = parseContainer(expected, eKind, eInner);
            if (aIsC && eIsC && aKind == eKind){
                auto splitTop = [](const std::string& s){
                    std::vector<std::string> parts;
                    int depth = 0;
                    std::size_t start = 0;
                    for (std::size_t i = 0; i < s.size(); ++i){
                        char c = s[i];
                        if (c == '<') depth++;
                        else if (c == '>') depth--;
                        else if (c == ',' && depth == 0){
                            parts.push_back(s.substr(start, i - start));
                            start = i + 1;
                        }
                    }
                    parts.push_back(s.substr(start));
                    for (auto& p : parts){
                        while (!p.empty() && p.front() == ' ') p.erase(p.begin());
                        while (!p.empty() && p.back()  == ' ') p.pop_back();
                    }
                    return parts;
                };
                auto aParts = splitTop(aInner);
                auto eParts = splitTop(eInner);
                if (aParts.size() == eParts.size()){
                    bool allMatch = true;
                    for (std::size_t i = 0; i < aParts.size(); ++i){
                        if (!typeCompatible(aParts[i], eParts[i])){
                            allMatch = false;
                            break;
                        }
                    }
                    if (allMatch) return true;
                }
            }
        }

        if (lastTypeSegment(actual) == lastTypeSegment(expected)) return true;
        if (isIntegerType(actual) && isIntegerType(expected)) return true;
        if (isNumericType(actual) && isNumericType(expected)) return true;

        // ★ 有 _bool_ 的自定义类 → bool 隐式转换
        if (expected == "bool"){
            std::string cls = lastTypeSegment(actual);
            if (_classesWithBoolOp.count(cls)) return true;
        }
        return false;
    }

    // ★ 从 "vox_mod_Code_async::Task<int64>" 抠出 ["int64"]
    //   从 "Dict<string, List<int>>" 抠出 ["string", "List<int>"]
    static std::vector<std::string> extractTypeArgs(const std::string& typeName){
        auto lt = typeName.find('<');
        if (lt == std::string::npos) return {};
        if (typeName.empty() || typeName.back() != '>') return {};

        std::string inner = typeName.substr(lt + 1, typeName.size() - lt - 2);
        std::vector<std::string> result;

        auto trim = [](std::string s){
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
                s.erase(s.begin());
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                s.pop_back();
            return s;
        };

        int depth = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i < inner.size(); ++i){
            char c = inner[i];
            if (c == '<') depth++;
            else if (c == '>') depth--;
            else if (c == ',' && depth == 0){
                result.push_back(trim(inner.substr(start, i - start)));
                start = i + 1;
            }
        }
        std::string last = trim(inner.substr(start));
        if (!last.empty()) result.push_back(last);

        return result;
    }

    // ★ 从接收者 AST 推断出类名（短名）
    std::string inferReceiverClass(const AstNodePtr& obj) const {
        if (!obj) return "";

        // new Cls(...) → Cls
        if (auto nn = std::dynamic_pointer_cast<NewNode>(obj)){
            std::string cls = nn->className;
            auto nsPos = cls.rfind("::");
            if (nsPos != std::string::npos) cls = cls.substr(nsPos + 2);
            auto ltPos = cls.find('<');
            if (ltPos != std::string::npos) cls = cls.substr(0, ltPos);
            return cls;
        }

        // 变量 → 查类型
        if (auto v = std::dynamic_pointer_cast<VariableNode>(obj)){
            // ★ 剥内部前缀
            std::string nm = v->name;
            static const std::string voxPrefix = "vox_g_";
            if (nm.size() > voxPrefix.size() &&
                nm.compare(0, voxPrefix.size(), voxPrefix) == 0){
                nm = nm.substr(voxPrefix.size());
            }

            if (_knownClasses.count(nm) > 0)      return nm;
            if (_knownInterfaces.count(nm) > 0)   return nm;

            std::string t = lookupVarType(nm);   // lookupVarType 也剥，双保险
            if (t.empty() || t == "?") return "";

            auto ltPos = t.find('<');
            if (ltPos != std::string::npos) t = t.substr(0, ltPos);
            auto nsPos = t.rfind("::");
            if (nsPos != std::string::npos) t = t.substr(nsPos + 2);
            return t;
        }

        // self → 当前类
        if (std::dynamic_pointer_cast<SelfNode>(obj)){
            return _currentClassName;
        }

        // super → 基类
        if (auto sn = std::dynamic_pointer_cast<SuperNode>(obj)){
            return lastTypeSegment(sn->superClass);
        }

        // ★ namespace.var → 查变量表（window.draw / code.canvas）
        if (auto m = std::dynamic_pointer_cast<MemberAccessNode>(obj)){
            if (auto v = std::dynamic_pointer_cast<VariableNode>(m->object)){
                // 是不是已知命名空间？
                if (_knownNamespaces.count(v->name) > 0){
                    auto nsIt = _namespaceVars.find(v->name);
                    if (nsIt != _namespaceVars.end()){
                        auto vIt = nsIt->second.find(m->member);
                        if (vIt != nsIt->second.end()){
                            std::string t = vIt->second;
                            // 剥泛型
                            auto ltPos = t.find('<');
                            if (ltPos != std::string::npos) t = t.substr(0, ltPos);
                            // 剥 namespace
                            auto nsPos = t.rfind("::");
                            if (nsPos != std::string::npos) t = t.substr(nsPos + 2);
                            return t;
                        }
                    }
                }
            }
        }

        // 嵌套 MemberAccess（window.draw / a.b.c）→ 太复杂，放弃
        return "";
    }

    static std::string callCalleeName(const AstNodePtr& callee){
        if (auto v = std::dynamic_pointer_cast<VariableNode>(callee)){
            return v->name;
        }
        // ★ new Cls(...) → "Cls"
        if (auto nn = std::dynamic_pointer_cast<NewNode>(callee)){
            std::string cls = nn->className;
            // 剥 namespace 前缀 "vox_mod_Code_re::Regex" → "Regex"
            auto nsPos = cls.rfind("::");
            if (nsPos != std::string::npos) cls = cls.substr(nsPos + 2);
            // 剥泛型 "Regex<int>" → "Regex"
            auto ltPos = cls.find('<');
            if (ltPos != std::string::npos) cls = cls.substr(0, ltPos);
            return cls;
        }
        if (auto m = std::dynamic_pointer_cast<MemberAccessNode>(callee)){
            // ★ 递归解析 object
            std::string obj = callCalleeName(m->object);
            if (obj.empty()) return m->member;

            // 剥掉命名空间前缀
            auto nsPos = obj.rfind("::");
            if (nsPos != std::string::npos) obj = obj.substr(nsPos + 2);

            // ★ 只保留最后一段（去掉 "a.b." 前缀）
            auto dotPos = obj.rfind('.');
            if (dotPos != std::string::npos){
                obj = obj.substr(dotPos + 1);
            }

            return obj + "." + m->member;
        }
        return "";
    }

    void checkCallArgs(CallNode& call){
        std::string name;

        // ★ 优先用 receiver 类型推断（和 checkCallArgCount 保持一致）
        if (auto m = std::dynamic_pointer_cast<MemberAccessNode>(call.callee)){
            AstNodePtr obj = m->object;
            while (auto mm = std::dynamic_pointer_cast<MemberAccessNode>(obj)){
                obj = mm->object;
            }
            if (std::dynamic_pointer_cast<SelfNode>(obj)){
                return;   // self 开头无法确定类型 → 跳过（避免继承链误报）
            }
            std::string cls = inferReceiverClass(m->object);
            if (cls.empty()) return;
            name = cls + "." + m->member;
        } else {
            name = callCalleeName(call.callee);
            if (name.empty()) return;
        }

        const std::vector<std::vector<Param>>* sigsPtr = findSignatureList(name);
        if (!sigsPtr) return;
        const auto& allSigs = *sigsPtr;
        if (allSigs.empty()) return;

        // 1) 收集所有参数个数匹配的重载
        std::vector<const std::vector<Param>*> candidates;
        for (const auto& sig : allSigs){
            if (sig.size() == call.args.size()){
                candidates.push_back(&sig);
            }
        }
        // 没找到完全匹配的：退而求其次找 variadic 签名
        if (candidates.empty()){
            for (const auto& sig : allSigs){
                for (const auto& p : sig){
                    if (p.isVariadic){ candidates.push_back(&sig); break; }
                }
            }
        }
        if (candidates.empty()) return;   // 都不匹配 -> 交给 C++ 编译期处理

        // 2) 只要有一个重载参数类型兼容，就通过
        bool anyMatch = false;
        for (auto* cand : candidates){
            bool hasVariadic = false;
            for (const auto& p : *cand){
                if (p.isVariadic){ hasVariadic = true; break; }
            }
            if (hasVariadic){ anyMatch = true; break; }

            bool sigOk = true;
            std::size_t n = std::min(call.args.size(), cand->size());
            for (std::size_t i = 0; i < n; ++i){
                std::string actual   = inferExprType(call.args[i]);
                std::string expected = (*cand)[i].type;
                if (!typeCompatible(actual, expected)){
                    sigOk = false;
                    break;
                }
            }
            if (sigOk){ anyMatch = true; break; }
        }
        if (anyMatch) return;

        // 3) 一个都没匹配上，用第一个候选报错
        const std::vector<Param>& params = *candidates[0];
        std::size_t n = std::min(call.args.size(), params.size());
        for (std::size_t i = 0; i < n; ++i){
            std::string actual   = inferExprType(call.args[i]);
            std::string expected = params[i].type;
            if (!typeCompatible(actual, expected)){
                // ★ 报错前剥短名
                std::string shortName = name;
                auto nsPos = shortName.rfind("::");
                if (nsPos != std::string::npos){
                    shortName = shortName.substr(nsPos + 2);
                }
                // ★ 再剥 "." 前面的类路径： "Screen.onScreen" → "Screen.onScreen"
                //   （如果想更短可以只留 "." 后面的部分，看你的偏好）

                raise(EX_TYPE,
                    "'" + shortName + "' argument " + std::to_string(i + 1)
                    + " ('" + params[i].name + "') must be '"
                    + expected + "', not '" + actual + "'",
                    call.line);
            }
        }
    }

    // ---- 除零（常量） ----
    void checkConstantDivByZero(BinaryNode& n){
        if (n.op != TokenType::Div && n.op != TokenType::Mod) return;
        bool isZero = false;
        if (auto num = std::dynamic_pointer_cast<NumberNode>(n.right)){
            if (num->value == 0) isZero = true;
        } else if (auto fl = std::dynamic_pointer_cast<FloatNode>(n.right)){
            if (fl->value == 0.0) isZero = true;
        }
        if (isZero){
            raise(EX_ZERO_DIVISION, "division by zero", n.line);
        }
    }

    // ---- 数组越界（常量） ----
    void checkConstantIndexOutOfRange(IndexNode& n){
        if (auto ll = std::dynamic_pointer_cast<ListLiteralNode>(n.object)){
            if (auto num = std::dynamic_pointer_cast<NumberNode>(n.index)){
                if (num->value < 0 || num->value >= (int64_t)ll->items.size()){
                    raise(EX_INDEX_OUT,
                          "index " + std::to_string(num->value)
                          + " out of range for list of size "
                          + std::to_string(ll->items.size()),
                          n.line);
                }
            }
        }
    }

    // ---- is 目标类型 ----
    void checkIsTargetType(IsNode& n){
        std::string t = n.targetType;
        std::string last = t;
        auto pos = last.rfind("::");
        if (pos != std::string::npos) last = last.substr(pos + 2);

        if (last == "int" || last == "string" || last == "bool" || last == "char" ||
            last == "float" || last == "double" || last == "void" || last == "any" ||
            last == "int8" || last == "int16" || last == "int32" || last == "int64" ||
            last == "uint8" || last == "uint16" || last == "uint32" || last == "uint64"){
            return;
        }
        if (!_knownClasses.count(last) && !_knownInterfaces.count(last) &&
            !_knownEnums.count(last) && !_knownExceptions.count(last)){
            raise(EX_NAME, "unknown type '" + t + "' in 'is'", n.line);
        }
    }

    // ---- new 类存在 ----
    void checkNewClassExists(NewNode& n){
        std::string cls = n.className;
        auto ltPos = cls.find('<');
        if (ltPos != std::string::npos) cls = cls.substr(0, ltPos);
        auto pos = cls.rfind("::");
        if (pos != std::string::npos) cls = cls.substr(pos + 2);
        if (!_knownClasses.count(cls) && !_knownInterfaces.count(cls)){
            raise(EX_NAME, "class '" + cls + "' is not defined", n.line);
        }
        // ★ 拒绝实例化 abstract 类
        if (_abstractClassMethods.count(cls)){
            raise(EX_TYPE, "cannot instantiate abstract class '" + cls + "'", n.line);
        }
        // ★ interface 也不能实例化
        if (_knownInterfaces.count(cls) && !_knownClasses.count(cls)){
            raise(EX_TYPE, "cannot instantiate interface '" + cls + "'", n.line);
        }
    }

    // ---- 函数调用参数个数 ----
    void checkCallArgCount(CallNode& call){
        std::string name;

        if (auto m = std::dynamic_pointer_cast<MemberAccessNode>(call.callee)){
            // self 开头无法确定类型 → 跳过（避免继承链误报）
            AstNodePtr obj = m->object;
            while (auto mm = std::dynamic_pointer_cast<MemberAccessNode>(obj)){
                obj = mm->object;
            }
            if (std::dynamic_pointer_cast<SelfNode>(obj)){
                return;
            }

            // ★ 推断接收者类型 → "Window.fill"
            std::string cls = inferReceiverClass(m->object);
            if (cls.empty()) return;      // 推断不出 → 跳过
            name = cls + "." + m->member;
        } else {
            name = callCalleeName(call.callee);
            if (name.empty()) return;
        }

        const auto* sigsPtr = findSignatureList(name);
        if (!sigsPtr) return;

        const auto& allSigs = *sigsPtr;
        if (allSigs.empty()) return;

        bool anyCountOk = false;
        std::size_t minC = SIZE_MAX, maxC = 0;

        for (const auto& sig : allSigs){
            std::size_t sMin = 0, sMax = 0;
            bool hasVar = false;
            for (const auto& p : sig){
                if (p.isVariadic){ hasVar = true; continue; }
                sMax++;
                if (!p.defaultValue) sMin++;
            }
            if (hasVar) sMax = SIZE_MAX;
            if (call.args.size() >= sMin && call.args.size() <= sMax){
                anyCountOk = true;
                break;
            }
            if (sMin < minC) minC = sMin;
            if (sMax > maxC) maxC = sMax;
        }
        if (anyCountOk) return;

        std::string msg;
        if (maxC == SIZE_MAX){
            msg = "'" + name + "' expects at least " + std::to_string(minC)
                + " argument(s), but " + std::to_string(call.args.size()) + " given";
        } else if (minC == maxC){
            msg = "'" + name + "' expects " + std::to_string(minC)
                + " argument(s), but " + std::to_string(call.args.size()) + " given";
        } else {
            msg = "'" + name + "' expects " + std::to_string(minC)
                + " to " + std::to_string(maxC)
                + " argument(s), but " + std::to_string(call.args.size()) + " given";
        }
        raise(EX_ARGUMENT, msg, call.line);
    }

    // ---- new 参数个数 ----
    void checkNewArgCount(NewNode& n){
        // 剥泛型 + 命名空间前缀
        std::string cls = n.className;
        auto ltPos = cls.find('<');
        if (ltPos != std::string::npos) cls = cls.substr(0, ltPos);
        auto pos = cls.rfind("::");
        if (pos != std::string::npos) cls = cls.substr(pos + 2);

        const auto* sigsPtr = findSignatureList(cls);
        if (!sigsPtr) return;

        const auto& allSigs = *sigsPtr;
        if (allSigs.empty()) return;

        bool anyCountOk = false;
        std::size_t minC = SIZE_MAX, maxC = 0;

        for (const auto& sig : allSigs){
            std::size_t sMin = 0, sMax = 0;
            bool hasVar = false;
            for (const auto& p : sig){
                if (p.isVariadic){ hasVar = true; continue; }   // ★ 变参不限制上限
                sMax++;
                if (!p.defaultValue) sMin++;
            }
            if (hasVar) sMax = SIZE_MAX;
            if (n.args.size() >= sMin && n.args.size() <= sMax){
                anyCountOk = true;
                break;
            }
            if (sMin < minC) minC = sMin;
            if (sMax > maxC) maxC = sMax;
        }
        if (anyCountOk) return;

        std::string msg;
        if (maxC == SIZE_MAX){
            msg = "constructor of '" + cls + "' expects at least "
                + std::to_string(minC) + " argument(s), but "
                + std::to_string(n.args.size()) + " given";
        } else if (minC == maxC){
            msg = "constructor of '" + cls + "' expects "
                + std::to_string(minC) + " argument(s), but "
                + std::to_string(n.args.size()) + " given";
        } else {
            msg = "constructor of '" + cls + "' expects "
                + std::to_string(minC) + " to " + std::to_string(maxC)
                + " argument(s), but " + std::to_string(n.args.size()) + " given";
        }
        raise(EX_ARGUMENT, msg, n.line);
    }

    // ---- 未使用变量（警告） ----
    void emitUnusedVarWarnings(){
        for (const auto& kv : _varDeclLine){
            const std::string& nm = kv.first;
            if (nm.empty() || nm[0] == '_') continue;   // 下划线开头跳过
            if (_varUsed.count(nm)) continue;
            warn(kv.second, "'" + nm + "' is never used");
        }
    }

    // ---- 不可达代码（警告） ----
    void checkUnreachable(const AstNodePtr& node){
        if (!node) return;
        // ★ 跳过 namespace —— 标准库 / 子模块代码不该产生 warning
        if (std::dynamic_pointer_cast<NamespaceNode>(node)) return;

        if (auto blk = std::dynamic_pointer_cast<BlockNode>(node)){
            for (std::size_t i = 0; i < blk->statements.size(); ++i){
                auto s = blk->statements[i];
                if (std::dynamic_pointer_cast<ReturnNode>(s) ||
                    std::dynamic_pointer_cast<RaiseNode>(s) ||
                    std::dynamic_pointer_cast<BreakNode>(s) ||
                    std::dynamic_pointer_cast<ContinueNode>(s)){
                    if (i + 1 < blk->statements.size()){
                        int ln = nodeLine(blk->statements[i + 1]);
                        if (ln > 0){
                            warn(ln, "unreachable code");
                        }
                    }
                    break;
                }
            }
        }
        for (const auto& c : getChildren(node)){
            checkUnreachable(c);
        }
    }

    // ★ 把已知类型传播给表达式树中所有 NullNode
    //   处理：
    //     T x = null;
    //     T x = cond ? null : y;
    //     T x = cond ? y : null;
    //     x = null;   (x 是已知类型)
    //     self.field = null;
    //     return null;
    void propagateNullType(const AstNodePtr& node, const std::string& typeName){
        if (!node) return;
        if (typeName.empty() || typeName == "?" || typeName == "any") return;

        if (auto nn = std::dynamic_pointer_cast<NullNode>(node)){
            nn->explicitType = typeName;
            return;
        }
        if (auto tn = std::dynamic_pointer_cast<TernaryNode>(node)){
            propagateNullType(tn->thenExpr, typeName);
            propagateNullType(tn->elseExpr, typeName);
            return;
        }
        // 赋值表达式的最终值
        if (auto ae = std::dynamic_pointer_cast<AssignExprNode>(node)){
            if (ae->finalExpr) propagateNullType(ae->finalExpr, typeName);
            return;
        }
    }

    void typeCheckExpr(const AstNodePtr& node, bool asCallee = false, bool inBoolContext = false){
        if (!node) return;

        // ================= AssignExprNode =================
        if (auto n = std::dynamic_pointer_cast<AssignExprNode>(node)){
            for (const auto& p : n->assigns){
                if (p.target) typeCheckExpr(p.target);
                if (p.value)  typeCheckExpr(p.value);
                checkConstTarget(p.target, p.line);
            }
            if (n->finalExpr) typeCheckExpr(n->finalExpr);
            return;
        }

        // Lambda
        if (auto lam = std::dynamic_pointer_cast<LambdaNode>(node)){
            _typeScopeStack.push_back({});
            _constScopeStack.push_back({});

            // ★ 检查参数约束里的表达式（现在 constraint 是 ConstraintSet）
            for (const auto& p : lam->params){
                if (p.constraint.has_value()){
                    for (const auto& c : p.constraint->items){
                        for (const auto& a : c.args){
                            typeCheckExpr(a);
                        }
                    }
                }
            }

            for (const auto& p : lam->params){
                _typeScopeStack.back()[p.name] = p.type;
                if (p.isConst) _constScopeStack.back().insert(p.name);  // ★
            }
            std::string savedRet = _semCurrentReturnType;
            _semCurrentReturnType = lam->returnType;
            if (lam->body){
                for (const auto& s : lam->body->statements) typeCheckStmt(s);
            }
            _semCurrentReturnType = savedRet;
            _typeScopeStack.pop_back();
            _constScopeStack.pop_back();
            return;
        }

        // 未定义变量检查 + 标记使用
        if (auto v = std::dynamic_pointer_cast<VariableNode>(node)){
            // ★ 内部前缀 "vox_g_" 不暴露给用户 —— 剥掉后再做所有查找 / 报错
            std::string nm = v->name;
            static const std::string voxPrefix = "vox_g_";
            bool hadPrefix = (nm.size() > voxPrefix.size() &&
                            nm.compare(0, voxPrefix.size(), voxPrefix) == 0);
            if (hadPrefix){
                nm = nm.substr(voxPrefix.size());
            }

            std::string lastSeg = nm;
            auto nsPos = lastSeg.rfind("::");
            if (nsPos != std::string::npos){
                lastSeg = lastSeg.substr(nsPos + 2);
            }
            
            bool known = false;
            for (auto it = _typeScopeStack.rbegin();
            it != _typeScopeStack.rend(); ++it){
                if (it->count(nm)){ known = true; break; }
            }
            if (!known && _knownClasses.count(nm))          known = true;
            if (!known && _knownClasses.count(lastSeg))     known = true;
            if (!known && _knownInterfaces.count(nm))       known = true;
            if (!known && _knownInterfaces.count(lastSeg))  known = true;
            if (!known && _knownEnums.count(nm))            known = true;
            if (!known && _knownEnums.count(lastSeg))       known = true;
            if (!known && _knownNamespaces.count(nm))       known = true;
            if (!known && _knownExceptions.count(nm))       known = true;
            if (!known && _functionSignatures.count(nm))    known = true;
            if (!known && _functionSignatures.count(lastSeg)) known = true;
            if (!known && _templateParams.count(nm))        known = true;
            if (!known && _templateParams.count(lastSeg))   known = true;
            if (!known && _globalVarNames.count(nm))        known = true;
            if (!known && _globalVarNames.count(lastSeg))   known = true;
            if (!known){
                // ★ 报错用无前缀的"用户视角"名字
                raise(EX_NAME, "'" + nm + "' is not defined", v->line);
            }
            _varUsed.insert(nm);   // ★ 与 _varDeclLine 里的原始名一致
            
            // ★ 顶层变量 → 加前缀（只加一次）
            if (!hadPrefix){
                bool isLocal = false;
                for (std::size_t i = 1; i < _typeScopeStack.size(); ++i){
                    if (_typeScopeStack[i].count(nm)){ isLocal = true; break; }
                }
                if (!isLocal && _globalVarNames.count(nm) > 0){
                    v->name = "vox_g_" + nm;
                }
            }

            // ★ 多变量 foreach 的 optional 变量在 bool 语境下不展开
            //   用节点自身的字段，不依赖 _optionalLoopVars（typeCheck 阶段那个 set 已经空了）
            if (inBoolContext && v->isOptionalLoopVar){
                v->forceNoDeref = true;
            }

            return;
        }

        // 函数调用
        if (auto n = std::dynamic_pointer_cast<CallNode>(node)){
            for (const auto& a : n->args) typeCheckExpr(a);
            
            // ★ callee 是成员访问时，标记为 asCallee
            if (auto m = std::dynamic_pointer_cast<MemberAccessNode>(n->callee)){
                typeCheckExpr(m, /*asCallee=*/true);
            } else {
                typeCheckExpr(n->callee);
            }

            // ★ 类型作用域已建好，这里处理 variadic 展开 / null 特化
            processVariadicCalls(*n);

            checkCallArgs(*n);
            checkCallArgCount(*n);
            return;
        }

        // ★ 后置自增/自减：检查 target
        if (auto n = std::dynamic_pointer_cast<PostIncrementNode>(node)){
            typeCheckExpr(n->target);
            checkConstTarget(n->target, n->line);   // ★
            return;
        }

        if (auto n = std::dynamic_pointer_cast<TernaryNode>(node)){
            typeCheckExpr(n->condition, false, true);   // ★
            typeCheckExpr(n->thenExpr,  false, false);
            typeCheckExpr(n->elseExpr,  false, false);
            return;
        }

        // 一元运算
        if (auto n = std::dynamic_pointer_cast<UnaryNode>(node)){
            // ★ !x：x 是 bool 语境
            typeCheckExpr(n->operand, false, n->op == TokenType::Bang);
            if (n->op == TokenType::Sub && inferExprType(n->operand) == "any"){
                n->useAnyOps = true;
            }
            return;
        }

        // 二元运算：除零检查 + Any 分派
        if (auto n = std::dynamic_pointer_cast<BinaryNode>(node)){
            // ★ && / || 两侧是 bool 语境
            bool isLogic = (n->op == TokenType::AmpAmp ||
                            n->op == TokenType::PipePipe);
            typeCheckExpr(n->left,  false, isLogic);
            typeCheckExpr(n->right, false, isLogic);
            checkConstantDivByZero(*n);

            // ★ 任一侧是 any → 走运行时算术
            std::string lt = inferExprType(n->left);
            std::string rt = inferExprType(n->right);
            if (lt == "any" || rt == "any"){
                switch (n->op){
                case TokenType::Plus:
                case TokenType::Sub:
                case TokenType::Mul:
                case TokenType::Div:
                case TokenType::Mod:
                    n->useAnyOps = true;
                    break;
                default:
                    break;
                }
            }
            return;
        }

        // 下标：越界检查
        if (auto n = std::dynamic_pointer_cast<IndexNode>(node)){
            typeCheckExpr(n->object);
            typeCheckExpr(n->index);
            checkConstantIndexOutOfRange(*n);
            return;
        }

        // is：目标类型检查
        if (auto n = std::dynamic_pointer_cast<IsNode>(node)){
            typeCheckExpr(n->expr);
            checkIsTargetType(*n);
            return;
        }

        // new：类存在检查 + 参数个数检查
        if (auto n = std::dynamic_pointer_cast<NewNode>(node)){
            for (const auto& a : n->args) typeCheckExpr(a);
            checkNewClassExists(*n);
            checkNewArgCount(*n);
            return;
        }

        // ★ 成员访问：检查成员是否存在
        if (auto n = std::dynamic_pointer_cast<MemberAccessNode>(node)){
            typeCheckExpr(n->object);

            // 情况 A：object 是简单变量，类型是已知类 → 查类成员
            if (auto v = std::dynamic_pointer_cast<VariableNode>(n->object)){
                std::string objType = lookupVarType(v->name);

                // 命名空间访问（vox_mod_Code_window）
                if (n->namespaceAccess){
                    std::string ns = v->name;
                    // 剥掉 "::" 前缀
                    auto it = _namespaceMembers.find(ns);
                    if (it == _namespaceMembers.end()){
                        // 尝试找它的末段（vox_mod_Code_window_ui → ui）
                        for (const auto& kv : _namespaceMembers){
                            auto pos = kv.first.rfind("::");
                            std::string shortNs = (pos == std::string::npos)
                                                ? kv.first : kv.first.substr(pos + 2);
                            if (shortNs == ns){
                                it = _namespaceMembers.find(kv.first);
                                break;
                            }
                        }
                    }
                    if (it != _namespaceMembers.end() && it->second.count(n->member) == 0){
                        raise(EX_NAME,
                              "'" + n->member + "' is not defined in namespace '"
                              + ns + "'",
                              n->line);
                    }
                }
                // 类实例访问
                else if (!objType.empty() && objType != "?"){
                    std::string cls = lastTypeSegment(objType);
                    auto it = _classMembers.find(cls);
                    if (it != _classMembers.end() && it->second.count(n->member) == 0){
                        raise(EX_NAME,
                            "class '" + cls + "' has no member named '"
                            + n->member + "'",
                            n->line);
                    }
                    // ★ 成员必须是实例字段（除非作为 callee）
                    if (!asCallee){
                        auto fit = _classInstanceFields.find(cls);
                        if (fit != _classInstanceFields.end()
                            && fit->second.count(n->member) == 0){
                            raise(EX_TYPE,
                                "'" + n->member + "' is a method, not a field; "
                                "did you mean '" + n->member + "()'?",
                                n->line);
                        }
                    }
                }
            }
            // 情况 B：object 是 self → 查当前类
           else if (std::dynamic_pointer_cast<SelfNode>(n->object)){
                if (!_currentClassName.empty()){
                    // 成员存在性检查（字段或方法都可）
                    auto it = _classMembers.find(_currentClassName);
                    if (it != _classMembers.end() && it->second.count(n->member) == 0){
                        raise(EX_NAME,
                            "class '" + _currentClassName + "' has no member named '"
                            + n->member + "'",
                            n->line);
                    }
                    // ★ 新增：成员必须是实例字段（除非作为 callee）
                    if (!asCallee){
                        auto fit = _classInstanceFields.find(_currentClassName);
                        if (fit != _classInstanceFields.end()
                            && fit->second.count(n->member) == 0){
                            raise(EX_TYPE,
                                "'" + n->member + "' is a method, not a field; "
                                "did you mean '" + n->member + "()'?",
                                n->line);
                        }
                    }
                }
            }
            // 情况 C：其它（链式访问、a.b.c）→ 不检查

            return;
        }

        for (const auto& c : getChildren(node)) typeCheckExpr(c);
    }

    // ★ 检查赋值目标是否是 const（变量 / 字段 / self.field）
    void checkConstTarget(const AstNodePtr& target, int line){
        if (!target) return;

        // 简单变量：x = ...
        if (auto v = std::dynamic_pointer_cast<VariableNode>(target)){
            std::string nm = v->name;
            static const std::string voxPrefix = "vox_g_";
            if (nm.size() > voxPrefix.size() &&
                nm.compare(0, voxPrefix.size(), voxPrefix) == 0){
                nm = nm.substr(voxPrefix.size());
            }
            if (isConstVar(nm)){          // ★ 查表用无前缀名
                raise(EX_VALUE,
                    "cannot assign to const variable '" + nm + "'",
                    line);
            }
            return;
        }

        // 成员访问：obj.field / self.field
        if (auto ma = std::dynamic_pointer_cast<MemberAccessNode>(target)){
            std::string objType;
            if (auto ov = std::dynamic_pointer_cast<VariableNode>(ma->object)){
                objType = lookupVarType(ov->name);
            } else if (std::dynamic_pointer_cast<SelfNode>(ma->object)){
                objType = _currentClassName;
            }
            if (!objType.empty() && objType != "?"){
                std::string cls = lastTypeSegment(objType);
                auto it = _classConstFields.find(cls);
                if (it != _classConstFields.end() && it->second.count(ma->member)){
                    raise(EX_VALUE,
                        "cannot assign to const field '"
                        + cls + "." + ma->member + "'",
                        line);
                }
            }
        }
    }

    void typeCheckStmt(const AstNodePtr& node){
        if (!node) return;

        // ================= VariableDeclNode =================
        if (auto n = std::dynamic_pointer_cast<VariableDeclNode>(node)){
            // ★ 预期类型传播：从左侧类型抠出泛型参数，灌给 RHS 的泛型调用
            if (!n->values.empty()){
                auto typeArgs = extractTypeArgs(n->typeName);
                if (!typeArgs.empty()){
                    for (const auto& v : n->values){
                        auto call = std::dynamic_pointer_cast<CallNode>(v);
                        if (!call) continue;
                        if (!call->templateArgs.empty()) continue;   // 显式写了 <T>，不覆盖

                        std::string calleeName = callCalleeName(call->callee);
                        if (calleeName.empty()) continue;

                        // 用最后一段短名匹配： "vox_mod_Code_async.run" → "run"
                        std::string shortName = calleeName;
                        auto dotPos = shortName.rfind('.');
                        if (dotPos != std::string::npos){
                            shortName = shortName.substr(dotPos + 1);
                        }
                        auto nsPos = shortName.rfind("::");
                        if (nsPos != std::string::npos){
                            shortName = shortName.substr(nsPos + 2);
                        }

                        if (_genericFunctions.count(shortName) > 0 ||
                            _genericFunctions.count(calleeName) > 0){
                            call->templateArgs = typeArgs;
                        }
                    }
                }
            }

            // ★ 把声明类型传给 RHS 里的 NullNode（包括三元分支里的）
            if (n->typeName != "any"){
                for (const auto& v : n->values){
                    propagateNullType(v, n->typeName);
                }
            }

            for (const auto& v : n->values) typeCheckExpr(v);

            // ★ 初始化类型兼容检查
            for (std::size_t i = 0; i < n->names.size(); ++i){
                if (i >= n->values.size() || !n->values[i]) continue;

                // ★ any = null：any 映射到 C++ auto，没有类型提示 → 无法推导？现在从1.3.0开始使用vox::Any而不是直接映射为auto
                /*
                if (n->typeName == "any"
                    && std::dynamic_pointer_cast<NullNode>(n->values[i])){
                    raise(EX_TYPE,
                        "cannot initialize 'any' with 'null' — "
                        "'any' has no type hint, use a concrete type instead",
                        n->line);
                }
                */

                // 空 {} / [] 字面量对任何容器类型都兼容
                bool isEmptyLiteral = false;
                if (auto dl = std::dynamic_pointer_cast<DictLiteralNode>(n->values[i])){
                    if (dl->keys.empty()) isEmptyLiteral = true;
                }
                if (auto ll = std::dynamic_pointer_cast<ListLiteralNode>(n->values[i])){
                    if (ll->items.empty()) isEmptyLiteral = true;
                }
                if (isEmptyLiteral) continue;

                std::string actual = inferExprType(n->values[i]);
                if (!typeCompatible(actual, n->typeName)){
                    raise(EX_TYPE,
                          "cannot initialize '" + n->typeName + " "
                          + n->names[i] + "' with '" + actual + "'",
                          n->line);
                }
            }

            // ★ 登记未使用警告（跳过 namespace 内的模块代码）
            if (_nsDepth == 0){
                for (const auto& nm : n->names){
                    _varDeclLine[nm] = n->line;
                }
            }

            for (const auto& name : n->names){
                _typeScopeStack.back()[name] = n->typeName;
                if (n->isConst){
                    _constScopeStack.back().insert(name);
                }
            }
            return;
        }

        // ================= FunctionDeclNode =================
        if (auto n = std::dynamic_pointer_cast<FunctionDeclNode>(node)){
            _typeScopeStack.push_back({});
            _constScopeStack.push_back({});

            // ★ 模板参数入作用域
            std::vector<std::string> tmplAdded;
            for (const auto& p : n->templateParams){
                if (_templateParams.insert(p).second){
                    tmplAdded.push_back(p);
                }
            }

            std::string savedRet = _semCurrentReturnType;
            _semCurrentReturnType = n->returnType;

            for (const auto& p : n->params){
                _typeScopeStack.back()[p.name] =
                    p.isVariadic ? ("List<" + p.type + ">") : p.type;
                if (p.isConst) _constScopeStack.back().insert(p.name);   // ★
                if (p.defaultValue) typeCheckExpr(p.defaultValue);
            }
            if (n->body) for (const auto& s : n->body->statements) typeCheckStmt(s);

            _semCurrentReturnType = savedRet;

            for (const auto& p : tmplAdded){
                _templateParams.erase(p);
            }

            _typeScopeStack.pop_back();
            _constScopeStack.pop_back();
            return;
        }

        // ================= ClassNode =================
        if (auto n = std::dynamic_pointer_cast<ClassNode>(node)){
            _typeScopeStack.push_back({});
            _constScopeStack.push_back({});

            std::string savedCls = _currentClassName;   // ★
            _currentClassName = n->name;                // ★

            // ★ 模板参数入作用域
            std::vector<std::string> tmplAdded;
            for (const auto& p : n->templateParams){
                if (_templateParams.insert(p).second){
                    tmplAdded.push_back(p);
                }
            }

            for (const auto& f : n->fields){
                _typeScopeStack.back()[f.name] = f.typeName;
                if (f.isConst) _constScopeStack.back().insert(f.name);   // ★
                if (f.init) typeCheckExpr(f.init);
            }
            for (const auto& m : n->methods){
                _typeScopeStack.push_back({});
                _constScopeStack.push_back({});

                // ★ 把类的 const 字段注入方法作用域
                for (const auto& f : n->fields){
                    if (f.isConst) _constScopeStack.back().insert(f.name);
                }

                std::string savedRet = _semCurrentReturnType;
                _semCurrentReturnType = m.isCtor ? "" : m.returnType;

                for (const auto& p : m.params){
                    _typeScopeStack.back()[p.name] =
                        p.isVariadic ? ("List<" + p.type + ">") : p.type;
                    if (p.isConst) _constScopeStack.back().insert(p.name);   // ★
                    if (p.defaultValue) typeCheckExpr(p.defaultValue);
                }
                if (m.body) for (const auto& s : m.body->statements) typeCheckStmt(s);

                _semCurrentReturnType = savedRet;
                _typeScopeStack.pop_back();
                _constScopeStack.pop_back();
            }

            for (const auto& p : tmplAdded){
                _templateParams.erase(p);
            }

            _currentClassName = savedCls;               // ★ 恢复
            _typeScopeStack.pop_back();
            _constScopeStack.pop_back();
            return;
        }

        if (std::dynamic_pointer_cast<NamespaceNode>(node)){
            // ★ 标准库代码已经由 subParser 检查过一遍，不重复 typecheck
            //   （collectSignatures 仍然会遍历 namespace 注册签名）
            return;
        }
        if (std::dynamic_pointer_cast<ExternBlockNode>(node)) return;

        if (auto n = std::dynamic_pointer_cast<BlockNode>(node)){
            _typeScopeStack.push_back({});
            _constScopeStack.push_back({});
            for (const auto& s : n->statements) typeCheckStmt(s);
            _typeScopeStack.pop_back();
            _constScopeStack.pop_back();
            return;
        }

        if (auto n = std::dynamic_pointer_cast<IfNode>(node)){
            typeCheckExpr(n->condition, false, true);   // ★
            if (n->thenBody) typeCheckStmt(n->thenBody);
            if (n->elseBranch) typeCheckStmt(n->elseBranch);
            return;
        }

        if (auto n = std::dynamic_pointer_cast<TryNode>(node)){
            _typeScopeStack.push_back({});
            _constScopeStack.push_back({});
            if (n->tryBody) typeCheckStmt(n->tryBody);
            _typeScopeStack.pop_back();
            _constScopeStack.pop_back();
            for (const auto& c : n->catches){
                _typeScopeStack.push_back({});
                _constScopeStack.push_back({});
                _typeScopeStack.back()[c.variableName] = c.exceptionType;
                if (c.body) typeCheckStmt(c.body);
                _typeScopeStack.pop_back();
                _constScopeStack.pop_back();
            }
            _typeScopeStack.push_back({});
            _constScopeStack.push_back({});
            if (n->finallyBody) typeCheckStmt(n->finallyBody);
            _typeScopeStack.pop_back();
            _constScopeStack.pop_back();
            return;
        }

        // ================= SwitchNode（break 合法） =================
        if (auto n = std::dynamic_pointer_cast<SwitchNode>(node)){
            if (n->expr) typeCheckExpr(n->expr);

            // ★ switch 只支持整型 / char / bool / enum
            if (n->expr){
                std::string exprType = inferExprType(n->expr);
                bool ok = (exprType.empty() ||
                        exprType == "?" ||
                        isIntegerType(exprType) ||
                        exprType == "char" ||
                        exprType == "bool" ||
                        _knownEnums.count(lastTypeSegment(exprType)) > 0 ||
                        _knownExceptions.count(lastTypeSegment(exprType)) > 0);
                if (!ok){
                    // 剥掉 "vox_mod_Code::" 这类前缀，报错更友好
                    std::string cleanType = exprType;
                    auto nsPos = cleanType.rfind("::");
                    if (nsPos != std::string::npos){
                        cleanType = cleanType.substr(nsPos + 2);
                    }
                    raise(EX_TYPE,
                        "switch expression must be int, char, bool, or enum; "
                        "got '" + cleanType + "' — use 'if / else if' for "
                        "string comparison instead",
                        n->line);
                }
            }

            _typeScopeStack.push_back({});
            _constScopeStack.push_back({});
            _semSwitchDepth++;                              // ★
            for (const auto& c : n->cases){
                if (c.value) typeCheckExpr(c.value);
                if (c.body)  typeCheckStmt(c.body);
            }
            _semSwitchDepth--;                              // ★
            _typeScopeStack.pop_back();
            _constScopeStack.pop_back();
            return;
        }

        // ================= WithNode =================
        if (auto n = std::dynamic_pointer_cast<WithNode>(node)){   // ★
            int ln = current().line;
            if (!n->typeName.empty()){
                std::string shortType = n->typeName;
                auto pos = shortType.rfind("::");
                if (pos != std::string::npos) shortType = shortType.substr(pos + 2);
                if (_knownClasses.count(shortType) > 0 &&
                    _classesWithWith.count(shortType) == 0){
                    raise(EX_TYPE,
                        "class '" + shortType
                        + "' has no '_with_' method, cannot use 'with'",
                        ln);
                }
            }
            typeCheckExpr(n->init);

            _typeScopeStack.push_back({});
            _constScopeStack.push_back({});

            // ★ 变量在 with 块内可见；如果有类型名，直接登记；
            //   没有类型名就暂时登记为 "?"（auto 类型推断）
            std::string vt = n->typeName.empty() ? "?" : n->typeName;
            _typeScopeStack.back()[n->varName] = vt;

            if (n->body) typeCheckStmt(n->body);

            _typeScopeStack.pop_back();
            _constScopeStack.pop_back();
            return;
        }

        // Break
        if (auto b = std::dynamic_pointer_cast<BreakNode>(node)){
            if (_semLoopDepth == 0 && _semSwitchDepth == 0){
                raise(EX_SYNTAX, "'break' outside of loop or switch", b->line);
            }
            return;
        }
        // Continue
        if (auto c = std::dynamic_pointer_cast<ContinueNode>(node)){
            if (_semLoopDepth == 0){
                raise(EX_SYNTAX, "'continue' outside of loop", c->line);
            }
            return;
        }

        // ================= WhileNode =================
        if (auto n = std::dynamic_pointer_cast<WhileNode>(node)){
            typeCheckExpr(n->condition, false, true);   // ★
            _semLoopDepth++;
            if (n->body) typeCheckStmt(n->body);
            _semLoopDepth--;
            return;
        }

        // ================= DoWhileNode =================
        if (auto n = std::dynamic_pointer_cast<DoWhileNode>(node)){
            _semLoopDepth++;
            if (n->body) typeCheckStmt(n->body);
            _semLoopDepth--;
            typeCheckExpr(n->condition, false, true);   // ★
            return;
        }

        // ================= ForNode =================
        if (auto n = std::dynamic_pointer_cast<ForNode>(node)){
            _typeScopeStack.push_back({});
            _constScopeStack.push_back({});
            if (n->init) typeCheckStmt(n->init);
            if (n->cond) typeCheckExpr(n->cond);
            if (n->step) typeCheckStmt(n->step);
            _semLoopDepth++;                                // ★
            if (n->body) typeCheckStmt(n->body);
            _semLoopDepth--;                                // ★
            _typeScopeStack.pop_back();
            _constScopeStack.pop_back();
            return;
        }

        // ================= ForeachNode =================
        if (auto n = std::dynamic_pointer_cast<ForeachNode>(node)){
            _typeScopeStack.push_back({});
            _constScopeStack.push_back({});
            for (const auto& v : n->vars){
                if (v.iterable) typeCheckExpr(v.iterable);
                _typeScopeStack.back()[v.name] = v.type;
            }
            _semLoopDepth++;                                // ★
            if (n->body) typeCheckStmt(n->body);
            _semLoopDepth--;                                // ★
            _typeScopeStack.pop_back();
            _constScopeStack.pop_back();
            return;
        }

        if (auto n = std::dynamic_pointer_cast<ExpressionStatementNode>(node)){
            if (n->expr) typeCheckExpr(n->expr);
            return;
        }

        // ================= ReturnNode（返回类型检查） =================
        if (auto n = std::dynamic_pointer_cast<ReturnNode>(node)){
            if (n->expression) typeCheckExpr(n->expression);

            if (_semCurrentReturnType.empty() || _semCurrentReturnType == "void"){
                if (n->expression){
                    // ★ 特判 null：给出更精确的提示
                    if (std::dynamic_pointer_cast<NullNode>(n->expression)){
                        raise(EX_TYPE,
                            "void function cannot return 'null' — "
                            "use a bare 'return;' instead",
                            n->line);
                    }
                    raise(EX_TYPE, "void function cannot return a value", n->line);
                }
                return;
            }

            // ★ 把返回类型传给表达式里的 NullNode
            if (n->expression && _semCurrentReturnType != "any"){
                propagateNullType(n->expression, _semCurrentReturnType);
            }

            if (!n->expression){
                raise(EX_TYPE,
                    "function returning '" + _semCurrentReturnType
                    + "' must return a value",
                    n->line);
            }
            std::string actual = inferExprType(n->expression);
            if (!typeCompatible(actual, _semCurrentReturnType)){
                raise(EX_TYPE,
                      "return type mismatch: expected '"
                      + _semCurrentReturnType + "', got '" + actual + "'",
                      n->line);
            }
            return;
        }

        // ================= AssignmentNode（赋值类型检查） =================
        if (auto n = std::dynamic_pointer_cast<AssignmentNode>(node)){

            // ★ 简单变量赋值：先检查是否已定义，再决定是否加前缀
            if (!n->name.empty()){
                // 剥内部前缀
                std::string nm = n->name;
                static const std::string voxPrefix = "vox_g_";
                if (nm.size() > voxPrefix.size() &&
                    nm.compare(0, voxPrefix.size(), voxPrefix) == 0){
                    nm = nm.substr(voxPrefix.size());
                }

                // 局部作用域（索引 ≥ 1）里找
                bool isLocal = false;
                for (std::size_t i = 1; i < _typeScopeStack.size(); ++i){
                    if (_typeScopeStack[i].count(nm)){ isLocal = true; break; }
                }
                // 跨模块 / 当前文件的顶层变量
                bool isGlobal = _globalVarNames.count(nm) > 0;

                // ★ 未定义变量检查
                if (!isLocal && !isGlobal){
                    raise(EX_NAME, "'" + nm + "' is not defined", n->line);
                }

                // 决定最终名字
                if (!isLocal && isGlobal){
                    n->name = "vox_g_" + nm;
                } else {
                    n->name = nm;   // 规整为无前缀
                }
            }

            if (n->target) typeCheckExpr(n->target);
            if (n->value)  typeCheckExpr(n->value);

            checkConstTarget(n->target, n->line);

            // ★ 计算 LHS 类型（含 self.field 的情况，供类型兼容 + null 传播共用）
            std::string lhsType;
            if (!n->name.empty()){
                lhsType = lookupVarType(n->name);
            } else if (auto v = std::dynamic_pointer_cast<VariableNode>(n->target)){
                lhsType = lookupVarType(v->name);
            } else if (auto m = std::dynamic_pointer_cast<MemberAccessNode>(n->target)){
                if (std::dynamic_pointer_cast<SelfNode>(m->object)
                    && !_currentClassName.empty()){
                    auto fit = _classFieldTypes.find(_currentClassName);
                    if (fit != _classFieldTypes.end()){
                        auto ft = fit->second.find(m->member);
                        if (ft != fit->second.end()) lhsType = ft->second;
                    }
                }
            }
            if (!lhsType.empty() && lhsType != "?" && lhsType != "any" && n->value){
                propagateNullType(n->value, lhsType);
            }
            if (!lhsType.empty() && lhsType != "?" && n->value){
                // 空字面量兼容
                bool isEmptyLiteral = false;
                if (auto dl = std::dynamic_pointer_cast<DictLiteralNode>(n->value)){
                    if (dl->keys.empty()) isEmptyLiteral = true;
                }
                if (auto ll = std::dynamic_pointer_cast<ListLiteralNode>(n->value)){
                    if (ll->items.empty()) isEmptyLiteral = true;
                }
                if (!isEmptyLiteral){
                    std::string actual = inferExprType(n->value);
                    if (!typeCompatible(actual, lhsType)){
                        raise(EX_TYPE,
                            "cannot assign '" + actual
                            + "' to '" + lhsType + "'",
                            n->line);
                    }
                }
            }

            if (!n->name.empty() && isConstVar(n->name)){
                raise(EX_VALUE,
                    "cannot assign to const variable '" + n->name + "'",
                    n->line);
            }
            return;
        }

        // ================= AssignExprNode =================
        if (auto n = std::dynamic_pointer_cast<AssignExprNode>(node)){
            for (const auto& p : n->assigns){
                if (p.target) typeCheckExpr(p.target);
                if (p.value)  typeCheckExpr(p.value);
                checkConstTarget(p.target, p.line);   // ★ 用真实行号
            }
            if (n->finalExpr) typeCheckExpr(n->finalExpr);
            return;
        }

        if (auto n = std::dynamic_pointer_cast<RaiseNode>(node)){
            if (n->message) typeCheckExpr(n->message);
            return;
        }
        if (std::dynamic_pointer_cast<SemicolonNode>(node)) return;
        if (std::dynamic_pointer_cast<EndNode>(node))       return;

        for (const auto& c : getChildren(node)) typeCheckStmt(c);
    }

    // ========== 魔法方法签名校验 ==========
    void checkClassMagicMethods(ClassNode& cls){
        const std::string& selfName = cls.name;
        for (const auto& m : cls.methods){
            if (m.isDtor){
                if (!m.params.empty()){
                    raise(EX_TYPE,
                        cls.name + "._del_() must take 0 parameters",
                        m.line);
                }
                if (!m.returnType.empty() && m.returnType != "void"){
                    raise(EX_TYPE,
                        cls.name + "._del_() must return 'void', got '"
                        + m.returnType + "'",
                        m.line);
                }
            }
            else if (m.name == "_bool_"){
                if (m.params.size() != 0){
                    raise(EX_TYPE,
                          cls.name + "._bool_() must take 0 parameters",
                          m.line);
                }
                if (m.returnType != "bool"){
                    raise(EX_TYPE,
                          cls.name + "._bool_() must return 'bool', got '"
                          + m.returnType + "'",
                          m.line);
                }
            }
            else if (m.name == "_string_"){
                if (m.params.size() != 0){
                    raise(EX_TYPE,
                          cls.name + "._string_() must take 0 parameters",
                          m.line);
                }
                if (m.returnType != "string"){
                    raise(EX_TYPE,
                          cls.name + "._string_() must return 'string', got '"
                          + m.returnType + "'",
                          m.line);
                }
            }
            else if (m.name == "_eq_"  || m.name == "_ne_" ||
                     m.name == "_lt_"  || m.name == "_le_" ||
                     m.name == "_gt_"  || m.name == "_ge_"){
                if (m.params.size() != 1){
                    raise(EX_TYPE,
                          cls.name + "." + m.name
                          + "() must take exactly 1 parameter",
                          m.line);
                }
                if (m.returnType != "bool"){
                    raise(EX_TYPE,
                          cls.name + "." + m.name
                          + "() must return 'bool', got '" + m.returnType + "'",
                          m.line);
                }
                const std::string& pt = m.params[0].type;
                if (pt != selfName && lastTypeSegment(pt) != selfName){
                    raise(EX_TYPE,
                          cls.name + "." + m.name + "() parameter must be of type '"
                          + selfName + "', got '" + pt + "'",
                          m.line);
                }
            }
            else if (m.name == "_add_" || m.name == "_sub_" ||
                     m.name == "_mul_" || m.name == "_div_" ||
                     m.name == "_mod_"){
                if (m.params.size() != 1){
                    raise(EX_TYPE,
                          cls.name + "." + m.name
                          + "() must take exactly 1 parameter",
                          m.line);
                }
                if (m.returnType != selfName
                    && lastTypeSegment(m.returnType) != selfName){
                    raise(EX_TYPE,
                          cls.name + "." + m.name + "() must return '"
                          + selfName + "', got '" + m.returnType + "'",
                          m.line);
                }
                const std::string& pt = m.params[0].type;
                if (pt != selfName && lastTypeSegment(pt) != selfName){
                    raise(EX_TYPE,
                          cls.name + "." + m.name + "() parameter must be of type '"
                          + selfName + "', got '" + pt + "'",
                          m.line);
                }
            }
        }
    }

    void checkAllMagicMethods(const AstNodePtr& node){
        if (!node) return;
        if (auto cls = std::dynamic_pointer_cast<ClassNode>(node)){
            checkClassMagicMethods(*cls);
            return;   // class 内部已经查完
        }
        for (const auto& c : getChildren(node)){
            checkAllMagicMethods(c);
        }
    }

    AstNodePtr defaultValue(const std::string& typeName){
        if (typeName == "bool") return std::make_shared<BoolNode>(false);
        if (typeName == "int"   || typeName == "int8"   || typeName == "int16" ||
            typeName == "int32" || typeName == "int64"  ||
            typeName == "uint8" || typeName == "uint16" ||
            typeName == "uint32"|| typeName == "uint64")
            return std::make_shared<NumberNode>(0);
        if (typeName == "float")  return std::make_shared<FloatNode>(0.0, true);
        if (typeName == "double") return std::make_shared<FloatNode>(0.0, false);
        return nullptr;
    }

    // ========== 语义警告工具 ==========

    void warn(int line, const std::string& msg){
        std::string code = getLine(line);
        std::string w = "warning: " + msg + "\n";
        w += "  File <" + _fileName + "> line " + std::to_string(line) + "\n";
        if (!code.empty()) w += "    " + code + "\n";
        _warnings.push_back(w);
    }

    static int nodeLine(const AstNodePtr& n){
        if (!n) return 0;
        if (auto v = std::dynamic_pointer_cast<ReturnNode>(n))         return v->line;
        if (auto v = std::dynamic_pointer_cast<VariableDeclNode>(n))   return v->line;
        if (auto v = std::dynamic_pointer_cast<AssignmentNode>(n))     return v->line;
        if (auto v = std::dynamic_pointer_cast<BreakNode>(n))          return v->line;
        if (auto v = std::dynamic_pointer_cast<ContinueNode>(n))       return v->line;
        if (auto v = std::dynamic_pointer_cast<RaiseNode>(n))          return v->line;
        if (auto v = std::dynamic_pointer_cast<ExpressionStatementNode>(n)){
            if (v->expr){
                if (auto c = std::dynamic_pointer_cast<CallNode>(v->expr))     return c->line;
                if (auto b = std::dynamic_pointer_cast<BinaryNode>(v->expr))   return b->line;
                if (auto ix = std::dynamic_pointer_cast<IndexNode>(v->expr))   return ix->line;
                if (auto vv = std::dynamic_pointer_cast<VariableNode>(v->expr))return vv->line;
            }
        }
        return 0;
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

        std::size_t realEnd = end;
        while (realEnd > start && _text[realEnd - 1] == '\r'){
            realEnd--;
        }

        // ★ 展开 Tab 为 4 空格
        std::string expanded;
        for (std::size_t i = start; i < realEnd; ++i){
            char ch = _text[i];
            if (ch == '\t') expanded += "    ";
            else            expanded += ch;
        }
        return expanded;
    }

    [[noreturn]] void raise(int kind, const std::string& message, int line) const {
        throw CompileError(kind, message, _fileName, getLine(line), line);
    }
    [[noreturn]] void raise(int kind, const std::string& message) const {
        raise(kind, message, current().line);
    }
    // ★ 带 caret 的版本
    [[noreturn]] void raise(int kind, const std::string& message, int line,
                            int caretCol, const std::string& caretHint = "") const {
        throw CompileError(kind, message, _fileName, getLine(line), line,
                           caretCol, caretHint);
    }
};

inline bool Parser::blockAlwaysReturns(const std::shared_ptr<BlockNode>& blk){
    if (!blk || blk->statements.empty()) return false;
    // ★ 遍历所有语句：只要有一条能保证 return / raise，整块就保证返回
    //   （因为它后面的代码都是不可达的）
    for (const auto& s : blk->statements){
        if (Parser::stmtAlwaysReturns(s)) return true;
    }
    return false;
}

inline bool Parser::stmtAlwaysReturns(const AstNodePtr& node){
    if (!node) return false;

    if (std::dynamic_pointer_cast<ReturnNode>(node)) return true;
    if (std::dynamic_pointer_cast<RaiseNode>(node))  return true;

    if (auto blk = std::dynamic_pointer_cast<BlockNode>(node)){
        return Parser::blockAlwaysReturns(blk);
    }

    if (auto ifn = std::dynamic_pointer_cast<IfNode>(node)){
        if (!ifn->elseBranch) return false;           // 没有 else → 不保证
        bool thenR = Parser::blockAlwaysReturns(ifn->thenBody);
        bool elseR = false;
        if (auto elseBlk = std::dynamic_pointer_cast<BlockNode>(ifn->elseBranch)){
            elseR = Parser::blockAlwaysReturns(elseBlk);
        } else if (auto elseIf = std::dynamic_pointer_cast<IfNode>(ifn->elseBranch)){
            elseR = Parser::stmtAlwaysReturns(elseIf);
        }
        return thenR && elseR;
    }

    // try { ... } catch (...) { ... } finally { ... } —— 先不处理
    // 循环、switch、普通表达式都不算"保证返回"
    return false;
}

#endif