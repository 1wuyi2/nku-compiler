#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>

namespace FE::AST
{
    bool ASTChecker::visit(Initializer& node)
    {
        // 示例实现：单个初始化器的语义检查
        // 1) 访问初始化值表达式
        // 2) 将子表达式的属性拷贝到当前节点
        ASSERT(node.init_val && "Null initializer value");
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
        if (isDeclaredInCurrentScope(lval->entry))
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

        if (node.init)
        {
            result &= apply(*this, *node.init);
            node.attr = node.init->attr;

            VarAttr* stored = symTable.getSymbol(lval->entry);
            if (registered && stored && node.init->singleInit && node.init->attr.val.isConstexpr)
                stored->initList.push_back(node.init->attr.val.value);
        }
        else { node.attr = lval->attr; }

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
