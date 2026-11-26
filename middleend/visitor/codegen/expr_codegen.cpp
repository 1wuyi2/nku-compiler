#include <middleend/visitor/codegen/ast_codegen.h>

namespace ME
{

    // 张量值在 IR 中统一表示为连续元素存储的地址，元素类型与形状由 AST 提供。
    DataType ASTCodeGen::tensorElementType(FE::AST::Type* type)
    {
        ASSERT(type != nullptr);
        if (type->getTypeGroup() == FE::AST::TypeGroup::TENSOR)
            return convert(FE::AST::TypeFactory::getBasicType(type->getBaseType()));
        return convert(type);
    }

    Operand* ASTCodeGen::allocateTensor(DataType type, const std::vector<int>& dims)
    {
        ASSERT(!dims.empty());
        const size_t reg = getNewRegId();
        curFunc->getBlock(0)->insertFront(createAllocaInst(type, reg, dims));
        return getRegOperand(reg);
    }

    Operand* ASTCodeGen::tensorElementAddress(DataType type, Operand* base, Operand* index)
    {
        // opaque ptr：按元素类型进行线性 GEP，无需重新构造多维下标。
        const size_t reg = getNewRegId();
        insert(new GEPInst(type, DataType::I64, base, getRegOperand(reg), {}, {index}));
        return getRegOperand(reg);
    }

    Operand* ASTCodeGen::tensorElementLoad(DataType type, Operand* base, Operand* index)
    {
        Operand* addr = tensorElementAddress(type, base, index);
        const size_t reg = getNewRegId();
        insert(createLoadInst(type, addr, reg));
        return getRegOperand(reg);
    }

    void ASTCodeGen::forTensorElements(const std::vector<int>& dims,
        const std::function<void(Operand*)>& emit)
    {
        ASSERT(!dims.empty());
        // i64 计数，避免多维元素个数超出 i32 时发生溢出。
        Operand* count = getImmeI32Operand(1);
        for (int extent : dims)
        {
            ASSERT(extent > 0 && "Tensor operation needs a fixed shape");
            Operand* product = getRegOperand(getNewRegId());
            insert(new ArithmeticInst(Operator::MUL, DataType::I64, count, getImmeI32Operand(extent), product));
            count = product;
        }
        Block* pre = curBlock;
        Block* cond = createBlock();
        Block* body = createBlock();
        Block* end = createBlock();
        insert(createBranchInst(cond->blockId));

        enterBlock(cond);
        Operand* index = getRegOperand(getNewRegId());
        auto* phi = new PhiInst(DataType::I64, index);
        phi->addIncoming(getImmeI32Operand(0), getLabelOperand(pre->blockId));
        insert(phi);
        Operand* test = getRegOperand(getNewRegId());
        insert(new IcmpInst(DataType::I64, ICmpOp::SLT, index, count, test));
        insert(new BrCondInst(test, getLabelOperand(body->blockId), getLabelOperand(end->blockId)));

        enterBlock(body);
        emit(index);
        Operand* next = getRegOperand(getNewRegId());
        insert(new ArithmeticInst(Operator::ADD, DataType::I64, index, getImmeI32Operand(1), next));
        phi->addIncoming(next, getLabelOperand(curBlock->blockId));
        insert(createBranchInst(cond->blockId));
        enterBlock(end);
    }

    void ASTCodeGen::copyTensor(DataType type, const std::vector<int>& dims, Operand* dst, Operand* src)
    {
        if (dst == src) return;
        forTensorElements(dims, [&](Operand* index) {
            Operand* val = tensorElementLoad(type, src, index);
            Operand* addr = tensorElementAddress(type, dst, index);
            insert(createStoreInst(type, val, addr));
        });
    }

    Operand* ASTCodeGen::handleTensorUnary(FE::AST::UnaryExpr& node, Module* m)
    {
        ASSERT(node.op == FE::AST::Operator::ADD || node.op == FE::AST::Operator::SUB);
        Operand* source = evaluateExprValue(*node.expr, m);
        const DataType type = tensorElementType(node.attr.val.value.type);
        const auto& dims = node.attr.arrayDims;
        Operand* result = allocateTensor(type, dims);
        if (node.op == FE::AST::Operator::ADD)
        {
            copyTensor(type, dims, result, source);
            return result;
        }
        forTensorElements(dims, [&](Operand* index) {
            Operand* value = tensorElementLoad(type, source, index);
            Operand* neg = getRegOperand(getNewRegId());
            if (type == DataType::F32)
                insert(new ArithmeticInst(Operator::FMUL, type, getImmeF32Operand(-1.0f), value, neg));
            else
                insert(new ArithmeticInst(Operator::SUB, type, getImmeI32Operand(0), value, neg));
            Operand* addr = tensorElementAddress(type, result, index);
            insert(createStoreInst(type, neg, addr));
        });
        return result;
    }

    Operand* ASTCodeGen::handleTensorBinary(FE::AST::BinaryExpr& node, Module* m)
    {
        using AOp = FE::AST::Operator;
        if (node.op == AOp::MATMUL) ERROR("Tensor matrix multiplication is not implemented in stage 9");
        const DataType type = tensorElementType(node.attr.val.value.type);
        const auto& dims = node.attr.arrayDims;
        const bool lt = node.lhs->attr.val.value.type->getTypeGroup() == FE::AST::TypeGroup::TENSOR;
        const bool rt = node.rhs->attr.val.value.type->getTypeGroup() == FE::AST::TypeGroup::TENSOR;
        // 表达式（尤其是标量函数调用）各求值一次，不能放入元素循环中重复求值。
        Operand* left = evaluateExprValue(*node.lhs, m);
        Operand* right = evaluateExprValue(*node.rhs, m);
        if (!lt) left = convertOperand(convert(node.lhs->attr.val.value.type), type, left, curBlock);
        if (!rt) right = convertOperand(convert(node.rhs->attr.val.value.type), type, right, curBlock);
        Operand* result = allocateTensor(type, dims);
        Operator op = Operator::ADD;
        switch (node.op)
        {
            case AOp::ADD: op = type == DataType::F32 ? Operator::FADD : Operator::ADD; break;
            case AOp::SUB: op = type == DataType::F32 ? Operator::FSUB : Operator::SUB; break;
            case AOp::MUL: op = type == DataType::F32 ? Operator::FMUL : Operator::MUL; break;
            case AOp::DIV: op = type == DataType::F32 ? Operator::FDIV : Operator::DIV; break;
            case AOp::MOD: ASSERT(type == DataType::I32); op = Operator::MOD; break;
            default: ERROR("Unsupported tensor arithmetic operator");
        }
        forTensorElements(dims, [&](Operand* index) {
            Operand* a = lt ? tensorElementLoad(type, left, index) : left;
            Operand* b = rt ? tensorElementLoad(type, right, index) : right;
            Operand* value = getRegOperand(getNewRegId());
            insert(new ArithmeticInst(op, type, a, b, value));
            Operand* addr = tensorElementAddress(type, result, index);
            insert(createStoreInst(type, value, addr));
        });
        return result;
    }

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
        const DataType elementType = tensorElementType(attr->type);
        ASSERT(elementType == DataType::I32 || elementType == DataType::F32);
        const auto& dims = attr->arrayDims;
        const size_t count = node.indices ? node.indices->size() : 0;
        ASSERT(count <= dims.size() && "Too many array subscripts");
        const bool wholeTensor = attr->type->getTypeGroup() == FE::AST::TypeGroup::TENSOR && count == 0;
        if (forWrite)
        {
            ASSERT(!attr->isConstDecl && "Assignment target is not modifiable");
            ASSERT((wholeTensor || count == dims.size()) && "An array or subarray cannot be assigned as a scalar");
        }
        const bool isArrayParam = !dims.empty() && dims.front() == -1;
        if (!wholeTensor && !dims.empty() && !(isArrayParam && count == 0))
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
        if (node.attr.val.value.type->getTypeGroup() == FE::AST::TypeGroup::TENSOR)
            exprResult = handleTensorUnary(node, m);
        else
            exprResult = handleUnaryCalc(node, m);
    }

    void ASTCodeGen::handleAssign(FE::AST::LeftValExpr& lhs, FE::AST::ExprNode& rhs, Module* m)
    {
        Operand* ptr = resolveLValueAddress(lhs, m, true);

        Operand* value = evaluateExprValue(rhs, m);
        if (lhs.attr.val.value.type->getTypeGroup() == FE::AST::TypeGroup::TENSOR)
        {
            copyTensor(tensorElementType(lhs.attr.val.value.type), lhs.attr.arrayDims, ptr, value);
            exprResult = ptr;
            return;
        }
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

        if (node.attr.val.value.type->getTypeGroup() == FE::AST::TypeGroup::TENSOR)
            exprResult = handleTensorBinary(node, m);
        else
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
