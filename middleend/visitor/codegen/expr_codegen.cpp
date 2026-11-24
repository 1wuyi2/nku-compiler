#include <middleend/visitor/codegen/ast_codegen.h>

namespace ME
{
    void ASTCodeGen::visit(FE::AST::LeftValExpr& node, Module* m)
    {
        (void)m;
        exprResult = nullptr;
        ASSERT(curFunc != nullptr && curBlock != nullptr && "Left-value read requires a current function/block");
        ASSERT(node.entry != nullptr && node.isLval && "Invalid left-value expression");
        ASSERT((!node.indices || node.indices->empty()) && "Array code generation belongs to Advanced");

        Operand* ptr     = nullptr;
        DataType valType = DataType::UNK;
        const size_t localReg = name2reg.getReg(node.entry);
        if (localReg != static_cast<size_t>(-1))
        {
            ptr = getRegOperand(localReg);
            auto attrIt = reg2attr.find(localReg);
            ASSERT(attrIt != reg2attr.end() && "Local variable is missing IR attributes");
            valType = convert(attrIt->second.type);
        }
        else
        {
            auto attrIt = glbSymbols.find(node.entry);
            ASSERT(attrIt != glbSymbols.end() && "Variable has no local or global storage");
            ptr     = getGlobalOperand(node.entry->getName());
            valType = convert(attrIt->second.type);
        }
        ASSERT((valType == DataType::I32 || valType == DataType::F32) && "Unsupported scalar load type");

        lval2ptr[&node] = ptr;
        const size_t valueReg = getNewRegId();
        insert(createLoadInst(valType, ptr, valueReg));
        exprResult = getRegOperand(valueReg);
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
        ASSERT(lhs.entry != nullptr && lhs.isLval && "Assignment requires a modifiable left value");
        ASSERT((!lhs.indices || lhs.indices->empty()) && "Array assignment belongs to Advanced");

        Operand* ptr = nullptr;
        const size_t localReg = name2reg.getReg(lhs.entry);
        if (localReg != static_cast<size_t>(-1))
        {
            ptr = getRegOperand(localReg);
            auto attrIt = reg2attr.find(localReg);
            ASSERT(attrIt != reg2attr.end() && !attrIt->second.isConstDecl &&
                   "Assignment target is not modifiable");
        }
        else
        {
            auto attrIt = glbSymbols.find(lhs.entry);
            ASSERT(attrIt != glbSymbols.end() && "Assignment target has no storage");
            ASSERT(!attrIt->second.isConstDecl && "Assignment target is not modifiable");
            ptr = getGlobalOperand(lhs.entry->getName());
        }
        lval2ptr[&lhs] = ptr;

        Operand* value = evaluateExprValue(rhs, m);
        Block*   block = curBlock;
        ASSERT(block != nullptr && "Assignment RHS did not leave a current block");
        const DataType rhsType = convert(rhs.attr.val.value.type);
        const DataType lhsType = convert(lhs.attr.val.value.type);
        ASSERT((lhsType == DataType::I32 || lhsType == DataType::F32) && "Unsupported assignment type");
        value = convertOperand(rhsType, lhsType, value, block);
        block->insert(createStoreInst(lhsType, value, ptr));
        exprResult = value;
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
            case FE::AST::Operator::ASSIGN:
            {
                auto* lhs = dynamic_cast<FE::AST::LeftValExpr*>(node.lhs);
                ASSERT(lhs != nullptr && lhs->isLval && "Assignment lhs is not a left value");
                handleAssign(*lhs, *node.rhs, m);
                return;
            }
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
        exprResult = nullptr;
        ASSERT(node.exprs != nullptr && !node.exprs->empty() && "Empty comma expression");
        for (FE::AST::ExprNode* expr : *node.exprs)
        {
            ASSERT(expr != nullptr && "Null expression in comma expression");
            exprResult = evaluateExprValue(*expr, m);
        }
    }
}  // namespace ME
