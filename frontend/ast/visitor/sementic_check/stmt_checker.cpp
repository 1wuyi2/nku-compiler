#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>

namespace FE::AST
{
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
        if (!node.entry) return false;

        // Root pre-registers functions.  A different node stored under this
        // name means this declaration was the duplicate already diagnosed.
        auto found = funcDecls.find(node.entry);
        if (found == funcDecls.end())
            funcDecls.emplace(node.entry, &node);
        else if (found->second != &node)
            return false;

        Type* oldRetType   = curFuncRetType;
        bool  oldHasReturn = funcHasReturn;
        curFuncRetType     = node.retType;
        funcHasReturn      = false;

        enterScope();
        bool result = true;
        try
        {
            if (node.params)
                for (auto* param : *node.params)
                    if (param) result &= apply(*this, *param);

            if (auto* body = dynamic_cast<BlockStmt*>(node.body))
                result &= visitBlock(*body, false);
            else if (node.body)
                result &= apply(*this, *node.body);
        } catch (...)
        {
            exitScope();
            curFuncRetType = oldRetType;
            funcHasReturn  = oldHasReturn;
            throw;
        }
        exitScope();

        curFuncRetType = oldRetType;
        funcHasReturn  = oldHasReturn;
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

    bool ASTChecker::visit(ReturnStmt& node)
    {
        // TODO(Lab3-1): 实现返回语句的语义检查
        // 设置返回标记，检查作用域，检查返回值类型匹配
        (void)node;
        TODO("Lab3-1: Implement ReturnStmt semantic checking");
    }

    bool ASTChecker::visit(WhileStmt& node)
    {
        // TODO(Lab3-1): 实现while循环的语义检查
        // 检查作用域，访问条件表达式，管理循环深度，访问循环体
        (void)node;
        TODO("Lab3-1: Implement WhileStmt semantic checking");
    }

    bool ASTChecker::visit(IfStmt& node)
    {
        // TODO(Lab3-1): 实现if语句的语义检查
        // 检查作用域，访问条件表达式，分别访问then和else分支
        (void)node;
        TODO("Lab3-1: Implement IfStmt semantic checking");
    }

    bool ASTChecker::visit(BreakStmt& node)
    {
        // TODO(Lab3-1): 实现break语句的语义检查
        // 检查是否在循环内使用
        (void)node;
        TODO("Lab3-1: Implement BreakStmt semantic checking");
    }

    bool ASTChecker::visit(ContinueStmt& node)
    {
        // TODO(Lab3-1): 实现continue语句的语义检查
        // 检查是否在循环内使用
        (void)node;
        TODO("Lab3-1: Implement ContinueStmt semantic checking");
    }

    bool ASTChecker::visit(ForStmt& node)
    {
        // TODO(Lab3-1): 实现for循环的语义检查
        // 检查作用域，访问初始化、条件、步进表达式，管理循环深度
        (void)node;
        TODO("Lab3-1: Implement ForStmt semantic checking");
    }
}  // namespace FE::AST
