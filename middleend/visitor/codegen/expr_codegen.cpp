#include <middleend/visitor/codegen/ast_codegen.h>

namespace ME
{
    void ASTCodeGen::visit(FE::AST::LeftValExpr& node, Module* m)
    {
        // TODO(Lab 3-2): 生成左值表达式的取址/取值 IR
        // 查找变量位置（全局或局部），处理数组下标/GEP，必要时发出load
        (void)node;
        (void)m;
        exprResult = nullptr;
        TODO("Lab3-2: Implement LeftValExpr IR generation");
    }

    void ASTCodeGen::visit(FE::AST::LiteralExpr& node, Module* m)
    {
        (void)m;
        exprResult = nullptr;

        switch (node.literal.type->getBaseType())
        {
            case FE::AST::Type_t::BOOL: exprResult = getImmeI32Operand(node.literal.getBool() ? 1 : 0); break;
            case FE::AST::Type_t::INT:
            case FE::AST::Type_t::LL:  // treat as I32
                exprResult = getImmeI32Operand(node.literal.getInt());
                break;
            case FE::AST::Type_t::FLOAT: exprResult = getImmeF32Operand(node.literal.getFloat()); break;
            default: ERROR("Unsupported literal type");
        }
    }

    void ASTCodeGen::visit(FE::AST::UnaryExpr& node, Module* m)
    {
        ASSERT(curFunc != nullptr && curBlock != nullptr && "Unary expression requires a current function/block");
        ASSERT(node.expr != nullptr && "Unary expression has no operand");
        exprResult = nullptr;
        exprResult = handleUnaryCalc(node, m);
    }

    void ASTCodeGen::handleAssign(FE::AST::LeftValExpr& lhs, FE::AST::ExprNode& rhs, Module* m)
    {
        // TODO(Lab 3-2): 生成赋值语句的 IR（计算右值、类型转换、store 到左值地址）
        (void)lhs;
        (void)rhs;
        (void)m;
        TODO("Lab3-2: Implement assignment IR generation");
    }
    void ASTCodeGen::handleLogicalAnd(
        FE::AST::BinaryExpr& node, FE::AST::ExprNode& lhs, FE::AST::ExprNode& rhs, Module* m)
    {
        // TODO(Lab 3-2): 生成短路与的基本块与条件分支
        (void)node;
        (void)lhs;
        (void)rhs;
        (void)m;
        TODO("Lab3-2: Implement logical AND codegen");
    }
    void ASTCodeGen::handleLogicalOr(
        FE::AST::BinaryExpr& node, FE::AST::ExprNode& lhs, FE::AST::ExprNode& rhs, Module* m)
    {
        // TODO(Lab 3-2): 生成短路或的基本块与条件分支
        (void)node;
        (void)lhs;
        (void)rhs;
        (void)m;
        TODO("Lab3-2: Implement logical OR codegen");
    }
    void ASTCodeGen::visit(FE::AST::BinaryExpr& node, Module* m)
    {
        ASSERT(curFunc != nullptr && curBlock != nullptr && "Binary expression requires a current function/block");
        ASSERT(node.lhs != nullptr && node.rhs != nullptr && "Binary expression has a missing operand");
        exprResult = nullptr;

        switch (node.op)
        {
            case FE::AST::Operator::ASSIGN: ERROR("Assignment IR generation belongs to Lab3 stage 3B");
            case FE::AST::Operator::AND:
            case FE::AST::Operator::OR: ERROR("Short-circuit logical IR generation belongs to Lab3 stage 3D");
            default: break;
        }

        exprResult = handleBinaryCalc(*node.lhs, *node.rhs, node.op, m);
    }

    void ASTCodeGen::visit(FE::AST::CallExpr& node, Module* m)
    {
        // TODO(Lab 3-2): 生成函数调用 IR（准备参数、可选返回寄存器、发出call）
        (void)node;
        (void)m;
        exprResult = nullptr;
        TODO("Lab3-2: Implement CallExpr IR generation");
    }

    void ASTCodeGen::visit(FE::AST::CommaExpr& node, Module* m)
    {
        // TODO(Lab 3-2): 依序生成逗号表达式每个子表达式的 IR
        (void)node;
        (void)m;
        exprResult = nullptr;
        TODO("Lab3-2: Implement CommaExpr IR generation");
    }
}  // namespace ME
