#include <middleend/visitor/codegen/ast_codegen.h>

namespace ME
{
    Operand* ASTCodeGen::resolveLValueAddress(FE::AST::LeftValExpr& node, Module* m, bool forWrite)
    {
        ASSERT(curFunc != nullptr && curBlock != nullptr && "Left value requires a current function/block");
        ASSERT(node.entry != nullptr && node.isLval && "Invalid left value");

        Operand* base = nullptr;
        const FE::AST::VarAttr* attr = nullptr;
        const size_t localReg = name2reg.getReg(node.entry);
        if (localReg != static_cast<size_t>(-1))
        {
            auto it = reg2attr.find(localReg);
            ASSERT(it != reg2attr.end() && "Local variable is missing IR attributes");
            attr = &it->second;
            base = getRegOperand(localReg);
        }
        else
        {
            auto it = glbSymbols.find(node.entry);
            ASSERT(it != glbSymbols.end() && "Variable has no local or global storage");
            attr = &it->second;
            base = getGlobalOperand(node.entry->getName());
        }
        const DataType elementType = convert(attr->type);
        ASSERT(elementType == DataType::I32 || elementType == DataType::F32);
        const auto& dims = attr->arrayDims;
        const size_t count = node.indices ? node.indices->size() : 0;
        ASSERT(count <= dims.size() && "Too many array subscripts");
        if (forWrite)
        {
            ASSERT(!attr->isConstDecl && "Assignment target is not modifiable");
            ASSERT(count == dims.size() && "An array or subarray cannot be assigned as a scalar");
        }
        const bool isArrayParam = !dims.empty() && dims.front() == -1;
        if (!dims.empty() && !(isArrayParam && count == 0))
        {
            // 普通数组指向整个聚合对象；形参已退化为首个子对象的地址。
            // 因此普通数组需要前导 0，形参不需要，并去掉未知的首维。
            std::vector<Operand*> indices;
            if (!isArrayParam) indices.push_back(getImmeI32Operand(0));
            const std::vector<int> gepDims = isArrayParam
                ? std::vector<int>(dims.begin() + 1, dims.end()) : dims;
            if (node.indices)
                for (auto* index : *node.indices)
                {
                    ASSERT(index != nullptr);
                    // 下标只求值一次，并使用求值后的当前块进行转换。
                    Operand* value = evaluateExprValue(*index, m);
                    value = convertOperand(convert(index->attr.val.value.type), DataType::I32, value, curBlock);
                    indices.push_back(value);
                }
            // 部分下标访问产生子数组首元素地址，不对聚合类型执行 load。
            if (count < dims.size()) indices.push_back(getImmeI32Operand(0));
            const size_t addrReg = getNewRegId();
            insert(createGEP_I32Inst(elementType, base, gepDims, indices, addrReg));
            base = getRegOperand(addrReg);
        }
        lval2ptr[&node] = base;
        return base;
    }

    void ASTCodeGen::visit(FE::AST::LeftValExpr& node, Module* m)
    {
        exprResult = nullptr;
        Operand* ptr = resolveLValueAddress(node, m, false);
        const DataType valType = convert(node.attr.val.value.type);
        if (valType == DataType::PTR)
        {
            exprResult = ptr;
            return;
        }
        ASSERT(valType == DataType::I32 || valType == DataType::F32);
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
        Operand* ptr = resolveLValueAddress(lhs, m, true);

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
            const DataType parameterType = convert(parameter->attr.val.value.type);
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
