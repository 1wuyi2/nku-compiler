#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>

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

        bool isValidReturnType(Type* type)
        {
            if (!type || type->getTypeGroup() != TypeGroup::BASIC) return false;
            const Type_t kind = type->getBaseType();
            return kind == Type_t::VOID || kind == Type_t::INT || kind == Type_t::FLOAT;
        }

        bool isValidParameterType(Type* type)
        {
            if (!type) return false;
            if (type->getTypeGroup() == TypeGroup::POINTER)
                return type->getBaseType() == Type_t::INT || type->getBaseType() == Type_t::FLOAT;
            return type->getTypeGroup() == TypeGroup::BASIC &&
                   (type->getBaseType() == Type_t::INT || type->getBaseType() == Type_t::FLOAT);
        }

        bool isConditionType(Type* type) { return isScalar(type); }
    }  // namespace

    bool ASTChecker::visit(ExprStmt& node)
    {
        // 示例实现：表达式语句的语义检查
        // 空表达式直接通过，否则访问内部表达式
        if (!node.expr) return true;
        return apply(*this, *node.expr);
    }

    bool ASTChecker::visit(FuncDeclStmt& node)
    {
        // TODO(Lab3-1): 实现函数声明的语义检查
        // 检查作用域，记录函数信息，处理形参和函数体，检查返回语句
        if (!node.entry)
        {
            reportError(node, "function declaration has no name");
            return false;
        }

        // Root pre-registers functions.  A different node stored under this
        // name means this declaration was the duplicate already diagnosed.
        bool result = true;
        if (!symTable.isGlobalScope())
        {
            reportError(node, "function '" + node.entry->getName() + "' is not declared at global scope");
            result = false;
        }
        else
        {
            auto found = funcDecls.find(node.entry);
            if (found == funcDecls.end())
                funcDecls.emplace(node.entry, &node);
            else if (found->second != &node)
                result = false;
        }

        Type*  oldRetType    = curFuncRetType;
        bool   oldHasReturn  = funcHasReturn;
        bool   oldInFunction = inFunction;
        size_t oldLoopDepth  = loopDepth;
        curFuncRetType       = node.retType;
        funcHasReturn        = false;
        inFunction           = true;
        loopDepth            = 0;

        enterScope();
        try
        {
            if (!isValidReturnType(node.retType))
            {
                reportError(node, "invalid return type for function '" + node.entry->getName() + "'");
                result = false;
            }
            if (node.params)
                for (auto* param : *node.params)
                {
                    if (!param) continue;
                    if (!isValidParameterType(param->type))
                    {
                        reportError(*param,
                            "invalid type for parameter '" +
                                (param->entry ? param->entry->getName() : std::string("<null>")) + "'");
                        result = false;
                    }
                    result &= apply(*this, *param);
                }

            if (auto* body = dynamic_cast<BlockStmt*>(node.body))
                result &= visitBlock(*body, false);
            else if (node.body)
                result &= apply(*this, *node.body);
        } catch (...)
        {
            exitScope();
            curFuncRetType = oldRetType;
            funcHasReturn  = oldHasReturn;
            inFunction     = oldInFunction;
            loopDepth      = oldLoopDepth;
            throw;
        }
        exitScope();

        const bool isMain = node.entry->getName() == "main";
        // 本实验不要求完整的控制流路径证明。循环与完备条件分支可能
        // 无法由简单的 AST 结构判断；保留显式 return 的类型检查，
        // 并检查普通非 void 函数是否至少含有一条 return。
        if (node.retType && node.retType != voidType && !isMain && !funcHasReturn)
        {
            reportError(node, "non-void function '" + node.entry->getName() + "' has no return statement");
            result = false;
        }

        curFuncRetType = oldRetType;
        funcHasReturn  = oldHasReturn;
        inFunction     = oldInFunction;
        loopDepth      = oldLoopDepth;
        return result;
    }

    bool ASTChecker::visit(VarDeclStmt& node)
    {
        // TODO(Lab3-1): 实现变量声明语句的语义检查
        // 空声明直接通过，否则委托给变量声明处理
        if (!node.decl) return true;
        return apply(*this, *node.decl);
    }

    bool ASTChecker::visit(BlockStmt& node)
    {
        // TODO(Lab3-1): 实现块语句的语义检查
        // 进入新作用域，逐条访问语句，退出作用域
        return visitBlock(node, true);
    }

    bool ASTChecker::visitBlock(BlockStmt& node, bool createScope)
    {
        if (createScope) enterScope();
        bool result = true;
        try
        {
            if (node.stmts)
                for (auto* stmt : *node.stmts)
                    if (stmt) result &= apply(*this, *stmt);
        } catch (...)
        {
            if (createScope) exitScope();
            throw;
        }
        if (createScope) exitScope();
        return result;
    }

    bool ASTChecker::visitControlledStmt(StmtNode* node)
    {
        if (!node) return true;
        const bool createScope = dynamic_cast<VarDeclStmt*>(node) != nullptr;
        if (createScope) enterScope();
        bool result = false;
        try
        {
            result = apply(*this, *node);
        } catch (...)
        {
            if (createScope) exitScope();
            throw;
        }
        if (createScope) exitScope();
        return result;
    }

    bool ASTChecker::visit(ReturnStmt& node)
    {
        // TODO(Lab3-1): 实现返回语句的语义检查
        // 设置返回标记，检查作用域，检查返回值类型匹配
        if (!inFunction)
        {
            reportError(node, "return statement is not within a function");
            if (node.retExpr) apply(*this, *node.retExpr);
            return false;
        }

        funcHasReturn = true;
        if (!node.retExpr)
        {
            if (curFuncRetType != voidType)
            {
                reportError(node, "non-void function must return a value");
                return false;
            }
            return true;
        }

        const bool exprOk = apply(*this, *node.retExpr);
        if (curFuncRetType == voidType)
        {
            reportError(node, "void function must not return a value");
            return false;
        }
        if (!exprOk) return false;
        Type* exprType = node.retExpr->attr.val.value.type;
        if (!isScalar(curFuncRetType) || !isScalar(exprType))
        {
            reportError(node,
                "cannot return expression of type '" + (exprType ? exprType->toString() : std::string("<null>")) +
                    "' from function returning '" +
                    (curFuncRetType ? curFuncRetType->toString() : std::string("<null>")) + "'");
            return false;
        }
        return true;
    }

    bool ASTChecker::visit(WhileStmt& node)
    {
        // TODO(Lab3-1): 实现while循环的语义检查
        // 检查作用域，访问条件表达式，管理循环深度，访问循环体
        bool result = true;
        if (!node.cond)
        {
            reportError(node, "while statement requires a condition");
            result = false;
        }
        else
        {
            const bool condOk = apply(*this, *node.cond);
            result &= condOk;
            if (condOk && !isConditionType(node.cond->attr.val.value.type))
            {
                reportError(node, "while condition must have scalar type");
                result = false;
            }
        }

        ++loopDepth;
        try
        {
            result &= visitControlledStmt(node.body);
        } catch (...)
        {
            --loopDepth;
            throw;
        }
        --loopDepth;
        return result;
    }

    bool ASTChecker::visit(IfStmt& node)
    {
        // TODO(Lab3-1): 实现if语句的语义检查
        // 检查作用域，访问条件表达式，分别访问then和else分支
        bool result = true;
        if (!node.cond)
        {
            reportError(node, "if statement requires a condition");
            result = false;
        }
        else
        {
            const bool condOk = apply(*this, *node.cond);
            result &= condOk;
            if (condOk && !isConditionType(node.cond->attr.val.value.type))
            {
                reportError(node, "if condition must have scalar type");
                result = false;
            }
        }
        result &= visitControlledStmt(node.thenStmt);
        if (node.elseStmt) result &= visitControlledStmt(node.elseStmt);
        return result;
    }

    bool ASTChecker::visit(BreakStmt& node)
    {
        // TODO(Lab3-1): 实现break语句的语义检查
        // 检查是否在循环内使用
        if (loopDepth != 0) return true;
        reportError(node, "break statement is not within a loop");
        return false;
    }

    bool ASTChecker::visit(ContinueStmt& node)
    {
        // TODO(Lab3-1): 实现continue语句的语义检查
        // 检查是否在循环内使用
        if (loopDepth != 0) return true;
        reportError(node, "continue statement is not within a loop");
        return false;
    }

    bool ASTChecker::visit(ForStmt& node)
    {
        // TODO(Lab3-1): 实现for循环的语义检查
        // 检查作用域，访问初始化、条件、步进表达式，管理循环深度
        enterScope();
        bool result = true;
        try
        {
            if (node.init) result &= apply(*this, *node.init);
            if (node.cond)
            {
                const bool condOk = apply(*this, *node.cond);
                result &= condOk;
                if (condOk && !isConditionType(node.cond->attr.val.value.type))
                {
                    reportError(node, "for condition must have scalar type");
                    result = false;
                }
            }
            if (node.step) result &= apply(*this, *node.step);

            ++loopDepth;
            try
            {
                result &= visitControlledStmt(node.body);
            } catch (...)
            {
                --loopDepth;
                throw;
            }
            --loopDepth;
        } catch (...)
        {
            exitScope();
            throw;
        }
        exitScope();
        return result;
    }
}  // namespace FE::AST
