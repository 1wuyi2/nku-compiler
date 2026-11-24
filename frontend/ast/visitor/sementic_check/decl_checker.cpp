#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>
#include <cmath>
#include <limits>

namespace FE::AST
{
    namespace
    {
        bool isScalar(Type* type)
        {
            if (!type || type->getTypeGroup() != TypeGroup::BASIC) return false;
            const Type_t kind = type->getBaseType();
            return kind == Type_t::BOOL || kind == Type_t::INT || kind == Type_t::LL || kind == Type_t::FLOAT;
        }

        bool isValidVariableType(Type* type)
        {
            if (!type || type->getTypeGroup() != TypeGroup::BASIC) return false;
            const Type_t kind = type->getBaseType();
            return kind == Type_t::INT || kind == Type_t::FLOAT;
        }

        bool convertConstant(const VarValue& source, Type* target, VarValue& result)
        {
            if (!isScalar(source.type) || !isScalar(target)) return false;

            switch (target->getBaseType())
            {
                case Type_t::BOOL: result = VarValue(source.getBool()); return true;
                case Type_t::INT:
                {
                    long double value = 0;
                    switch (source.type->getBaseType())
                    {
                        case Type_t::BOOL: value = source.boolValue; break;
                        case Type_t::INT: value = source.intValue; break;
                        case Type_t::LL: value = source.llValue; break;
                        case Type_t::FLOAT: value = source.floatValue; break;
                        default: return false;
                    }
                    if (!std::isfinite(value) || value < std::numeric_limits<int>::min() ||
                        value > std::numeric_limits<int>::max())
                        return false;
                    result = VarValue(static_cast<int>(value));
                    return true;
                }
                case Type_t::LL:
                {
                    long double value = 0;
                    switch (source.type->getBaseType())
                    {
                        case Type_t::BOOL: value = source.boolValue; break;
                        case Type_t::INT: value = source.intValue; break;
                        case Type_t::LL: value = source.llValue; break;
                        case Type_t::FLOAT: value = source.floatValue; break;
                        default: return false;
                    }
                    if (!std::isfinite(value) || value < std::numeric_limits<long long>::min() ||
                        value > std::numeric_limits<long long>::max())
                        return false;
                    result = VarValue(static_cast<long long>(value));
                    return true;
                }
                case Type_t::FLOAT: result = VarValue(source.getFloat()); return true;
                default: return false;
            }
        }
    }  // namespace

    bool ASTChecker::visit(Initializer& node)
    {
        // 示例实现：单个初始化器的语义检查
        // 1) 访问初始化值表达式
        // 2) 将子表达式的属性拷贝到当前节点
        if (!node.init_val)
        {
            reportError(node, "missing initializer expression");
            node.attr.val.value.type  = voidType;
            node.attr.val.isConstexpr = false;
            return false;
        }
        bool res  = apply(*this, *node.init_val);
        node.attr = node.init_val->attr;
        return res;
    }

    bool ASTChecker::visit(InitializerList& node)
    {
        // 示例实现：初始化器列表的语义检查
        // 遍历列表中的每个初始化器并逐个访问
        if (!node.init_list) return true;
        bool res = true;
        for (auto* init : *(node.init_list))
        {
            if (!init) continue;
            res &= apply(*this, *init);
        }
        return res;
    }

    bool ASTChecker::visit(VarDeclarator& node)
    {
        // TODO(Lab3-1): 实现变量声明器的语义检查
        // 访问左值表达式，同步属性，处理初始化器（如果有）
        auto* lval = dynamic_cast<LeftValExpr*>(node.lval);
        if (!lval || !lval->entry)
        {
            reportError(node, "invalid variable declarator");
            return false;
        }

        bool result     = true;
        bool registered = false;
        if (symTable.isGlobalScope() && funcDecls.count(lval->entry) != 0)
        {
            reportError(node, "global variable '" + lval->entry->getName() + "' conflicts with a function");
            result = false;
        }
        else if (isDeclaredInCurrentScope(lval->entry))
        {
            reportError(node, "redefinition of variable '" + lval->entry->getName() + "'");
            result = false;
        }
        else
        {
            VarAttr varAttr(currentDeclType ? currentDeclType : voidType, currentDeclIsConst, symTable.getScopeDepth());
            symTable.addSymbol(lval->entry, varAttr);
            rememberCurrentScope(lval->entry);
            registered = true;

            lval->isLval               = true;
            lval->attr.val.value.type  = varAttr.type;
            lval->attr.val.isConstexpr = false;

            if (lval->indices)
            {
                for (auto* index : *lval->indices)
                    if (index) result &= apply(*this, *index);
            }
        }

        const bool isScalarDeclarator = !lval->indices || lval->indices->empty();
        if (node.init)
        {
            const bool initOk = apply(*this, *node.init);
            result &= initOk;
            node.attr = node.init->attr;

            VarAttr* stored = symTable.getSymbol(lval->entry);
            if (registered && stored && isScalarDeclarator)
            {
                if (!node.init->singleInit)
                {
                    reportError(
                        node, "initializer list is not valid for scalar variable '" + lval->entry->getName() + "'");
                    result = false;
                }
                else if (initOk)
                {
                    Type* initType = node.init->attr.val.value.type;
                    if (!isScalar(stored->type) || !isScalar(initType))
                    {
                        reportError(node,
                            "cannot initialize variable '" + lval->entry->getName() + "' of type '" +
                                stored->type->toString() + "' with expression of type '" +
                                (initType ? initType->toString() : std::string("<null>")) + "'");
                        result = false;
                    }
                    else if (symTable.isGlobalScope() && !node.init->attr.val.isConstexpr)
                    {
                        reportError(node,
                            "initializer for global variable '" + lval->entry->getName() +
                                "' is not a constant expression");
                        result = false;
                    }
                    else if (stored->isConstDecl && !node.init->attr.val.isConstexpr)
                    {
                        reportError(node,
                            "initializer for const variable '" + lval->entry->getName() +
                                "' is not a constant expression");
                        result = false;
                    }
                    else if (node.init->attr.val.isConstexpr)
                    {
                        VarValue converted;
                        if (!convertConstant(node.init->attr.val.value, stored->type, converted))
                        {
                            reportError(node,
                                "constant initializer is out of range for variable '" + lval->entry->getName() + "'");
                            result = false;
                        }
                        else
                            stored->initList.push_back(converted);
                    }
                }
            }
        }
        else
        {
            node.attr = lval->attr;
            if (registered && currentDeclIsConst && isScalarDeclarator)
            {
                reportError(node, "const variable '" + lval->entry->getName() + "' requires an initializer");
                result = false;
            }
        }

        if (registered && symTable.isGlobalScope())
        {
            VarAttr* stored = symTable.getSymbol(lval->entry);
            if (stored) glbSymbols[lval->entry] = *stored;
        }
        return result;
    }

    bool ASTChecker::visit(ParamDeclarator& node)
    {
        // TODO(Lab3-1): 实现函数形参的语义检查
        // 检查形参重定义，处理数组形参的类型退化，将形参加入符号表
        if (!node.entry)
        {
            reportError(node, "invalid function parameter");
            return false;
        }
        if (isDeclaredInCurrentScope(node.entry))
        {
            reportError(node, "redefinition of parameter '" + node.entry->getName() + "'");
            return false;
        }

        VarAttr attr(node.type ? node.type : voidType, false, symTable.getScopeDepth());
        symTable.addSymbol(node.entry, attr);
        rememberCurrentScope(node.entry);
        node.attr.val.value.type  = attr.type;
        node.attr.val.isConstexpr = false;
        return true;
    }

    bool ASTChecker::visit(VarDeclaration& node)
    {
        // TODO(Lab3-1): 实现变量声明的语义检查
        // 遍历声明列表，检查重定义，处理数组维度和初始化，将符号加入符号表
        if (!node.decls) return true;

        Type* oldType      = currentDeclType;
        bool  oldIsConst   = currentDeclIsConst;
        currentDeclType    = node.type;
        currentDeclIsConst = node.isConstDecl;

        bool result = true;
        if (!isValidVariableType(node.type))
        {
            reportError(node,
                "variable declaration has invalid type '" +
                    (node.type ? node.type->toString() : std::string("<null>")) + "'");
            result = false;
        }
        try
        {
            for (auto* decl : *node.decls)
                if (decl) result &= apply(*this, *decl);
        } catch (...)
        {
            currentDeclType    = oldType;
            currentDeclIsConst = oldIsConst;
            throw;
        }

        currentDeclType    = oldType;
        currentDeclIsConst = oldIsConst;
        return result;
    }
}  // namespace FE::AST
