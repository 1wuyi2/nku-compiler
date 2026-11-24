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

        Operand*     ptr      = nullptr;
        DataType     valType  = DataType::UNK;
        const size_t localReg = name2reg.getReg(node.entry);
        if (localReg != static_cast<size_t>(-1))
        {
            ptr         = getRegOperand(localReg);
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

        lval2ptr[&node]       = ptr;
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

        Operand*     ptr      = nullptr;
        const size_t localReg = name2reg.getReg(lhs.entry);
        if (localReg != static_cast<size_t>(-1))
        {
            ptr         = getRegOperand(localReg);
            auto attrIt = reg2attr.find(localReg);
            ASSERT(attrIt != reg2attr.end() && !attrIt->second.isConstDecl && "Assignment target is not modifiable");
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
        (void)node;
        Operand* lhsCond    = evaluateCondition(lhs, m);
        Block*   lhsEnd     = curBlock;
        Block*   rhsBlock   = createBlock();
        Block*   mergeBlock = createBlock();
        insert(new BrCondInst(lhsCond, getLabelOperand(rhsBlock->blockId), getLabelOperand(mergeBlock->blockId)));

        enterBlock(rhsBlock);
        Operand* rhsCond = evaluateCondition(rhs, m);
        Block*   rhsEnd  = curBlock;
        insert(createBranchInst(mergeBlock->blockId));

        enterBlock(mergeBlock);
        const size_t resultReg = getNewRegId();
        auto*        phi       = new PhiInst(DataType::I1, getRegOperand(resultReg));
        phi->addIncoming(getImmeI32Operand(0), getLabelOperand(lhsEnd->blockId));
        phi->addIncoming(rhsCond, getLabelOperand(rhsEnd->blockId));
        insert(phi);
        exprResult = getRegOperand(resultReg);
    }
    void ASTCodeGen::handleLogicalOr(
        FE::AST::BinaryExpr& node, FE::AST::ExprNode& lhs, FE::AST::ExprNode& rhs, Module* m)
    {
        (void)node;
        Operand* lhsCond    = evaluateCondition(lhs, m);
        Block*   lhsEnd     = curBlock;
        Block*   rhsBlock   = createBlock();
        Block*   mergeBlock = createBlock();
        insert(new BrCondInst(lhsCond, getLabelOperand(mergeBlock->blockId), getLabelOperand(rhsBlock->blockId)));

        enterBlock(rhsBlock);
        Operand* rhsCond = evaluateCondition(rhs, m);
        Block*   rhsEnd  = curBlock;
        insert(createBranchInst(mergeBlock->blockId));

        enterBlock(mergeBlock);
        const size_t resultReg = getNewRegId();
        auto*        phi       = new PhiInst(DataType::I1, getRegOperand(resultReg));
        phi->addIncoming(getImmeI32Operand(1), getLabelOperand(lhsEnd->blockId));
        phi->addIncoming(rhsCond, getLabelOperand(rhsEnd->blockId));
        insert(phi);
        exprResult = getRegOperand(resultReg);
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
            case FE::AST::Operator::AND: handleLogicalAnd(node, *node.lhs, *node.rhs, m); return;
            case FE::AST::Operator::OR: handleLogicalOr(node, *node.lhs, *node.rhs, m); return;
            default: break;
        }

        exprResult = handleBinaryCalc(*node.lhs, *node.rhs, node.op, m);
    }

    void ASTCodeGen::visit(FE::AST::CallExpr& node, Module* m)
    {
        exprResult = nullptr;
        ASSERT(curFunc != nullptr && curBlock != nullptr && "Function call requires a current function/block");
        ASSERT(node.func != nullptr && "Function call has no callee");

        auto declIt = funcDecls.find(node.func);
        ASSERT(declIt != funcDecls.end() && declIt->second != nullptr && "Callee is missing its signature");
        FE::AST::FuncDeclStmt* callee         = declIt->second;
        const size_t           argumentCount  = node.args ? node.args->size() : 0;
        const size_t           parameterCount = callee->params ? callee->params->size() : 0;
        ASSERT(argumentCount == parameterCount && "Call argument count does not match its signature");

        CallInst::argList args;
        args.reserve(argumentCount);
        for (size_t i = 0; i < argumentCount; ++i)
        {
            FE::AST::ExprNode*        argument  = (*node.args)[i];
            FE::AST::ParamDeclarator* parameter = (*callee->params)[i];
            ASSERT(argument != nullptr && parameter != nullptr && "Invalid call argument or parameter");

            Operand* value = evaluateExprValue(*argument, m);
            Block*   block = curBlock;
            ASSERT(block != nullptr && !isCurrentBlockTerminated() && "Argument evaluation terminated its block");
            const DataType argumentType  = convert(argument->attr.val.value.type);
            const DataType parameterType = convert(parameter->type);
            ASSERT(
                parameterType != DataType::VOID && parameterType != DataType::UNK && "Invalid function parameter type");
            if (parameterType != DataType::PTR)
                value = convertOperand(argumentType, parameterType, value, block);
            else
                ASSERT(argumentType == DataType::PTR && "Pointer argument type mismatch");
            args.emplace_back(parameterType, value);
        }

        const DataType returnType = convert(callee->retType);
        ASSERT(returnType != DataType::UNK && returnType != DataType::PTR && "Invalid function return type");
        if (returnType == DataType::VOID)
        {
            insert(createCallInst(returnType, node.func->getName(), args));
            return;
        }

        const size_t resultReg = getNewRegId();
        insert(createCallInst(returnType, node.func->getName(), args, resultReg));
        exprResult = getRegOperand(resultReg);
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
