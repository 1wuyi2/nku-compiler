#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>

namespace FE::AST
{
    bool ASTChecker::visit(LeftValExpr& node)
    {
        // TODO(Lab3-1): 实现左值表达式的语义检查
        // 检查变量是否存在，处理数组下标访问，进行类型检查和常量折叠
        VarAttr* attr = symTable.getSymbol(node.entry);
        if (!attr)
        {
            const std::string name = node.entry ? node.entry->getName() : "<null>";
            reportError(node, "use of undeclared variable '" + name + "'");
            node.isLval               = false;
            node.attr.val.value.type  = voidType;
            node.attr.val.isConstexpr = false;
            return false;
        }

        bool result = true;
        if (node.indices)
        {
            for (auto* index : *node.indices)
                if (index) result &= apply(*this, *index);
        }

        node.isLval               = true;
        node.attr.val.value.type  = attr->type;
        node.attr.val.isConstexpr = false;
        if (attr->isConstDecl && !attr->initList.empty())
        {
            node.attr.val.value       = attr->initList.front();
            node.attr.val.isConstexpr = true;
        }
        return result;
    }

    bool ASTChecker::visit(LiteralExpr& node)
    {
        // 示例实现：字面量表达式的语义检查
        // 字面量总是编译期常量，直接设置属性
        node.attr.val.isConstexpr = true;
        node.attr.val.value       = node.literal;
        return true;
    }

    bool ASTChecker::visit(UnaryExpr& node)
    {
        // TODO(Lab3-1): 实现一元表达式的语义检查
        // 访问子表达式，检查操作数类型，调用类型推断函数
        (void)node;
        TODO("Lab3-1: Implement UnaryExpr semantic checking");
    }

    bool ASTChecker::visit(BinaryExpr& node)
    {
        // TODO(Lab3-1): 实现二元表达式的语义检查
        // 访问左右子表达式，检查操作数类型，调用类型推断
        (void)node;
        TODO("Lab3-1: Implement BinaryExpr semantic checking");
    }

    bool ASTChecker::visit(CallExpr& node)
    {
        // TODO(Lab3-1): 实现函数调用表达式的语义检查
        // 检查函数是否存在，访问实参列表，检查参数数量和类型匹配
        (void)node;
        TODO("Lab3-1: Implement CallExpr semantic checking");
    }

    bool ASTChecker::visit(CommaExpr& node)
    {
        // TODO(Lab3-1): 实现逗号表达式的语义检查
        // 依序访问各子表达式，结果为最后一个表达式的属性
        (void)node;
        TODO("Lab3-1: Implement CommaExpr semantic checking");
    }
}  // namespace FE::AST
