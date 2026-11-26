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

    Operand* ASTCodeGen::saveTensorTemps()
    {
        if (!hasDynamicTensorParams) return nullptr;
        const size_t reg = getNewRegId();
        insert(createCallInst(DataType::PTR, "llvm.stacksave", reg));
        return getRegOperand(reg);
    }

    void ASTCodeGen::releaseTensorTemps(Operand* saved)
    {
        if (!saved) return;
        CallInst::argList args = {{DataType::PTR, saved}};
        insert(createCallInst(DataType::VOID, "llvm.stackrestore", args));
    }

    Operand* ASTCodeGen::tensorFirstExtent(const std::vector<int>& dims, Operand* base)
    {
        ASSERT(!dims.empty());
        if (dims[0] > 0) return getImmeI32Operand(dims[0]);
        auto it = tensorExtents.find(base);
        ASSERT(dims[0] == -1 && it != tensorExtents.end());
        return it->second;
    }

    Operand* ASTCodeGen::tensorElementCount(const std::vector<int>& dims, Operand* first)
    {
        ASSERT(!dims.empty());
        Operand* count = getImmeI32Operand(1);
        for (size_t i = 0; i < dims.size(); ++i)
        {
            ASSERT(dims[i] > 0 || (i == 0 && dims[i] == -1 && first));
            Operand* extent = dims[i] == -1 ? first : getImmeI32Operand(dims[i]);
            Operand* product = getRegOperand(getNewRegId());
            insert(new ArithmeticInst(Operator::MUL, DataType::I64, count, extent, product));
            count = product;
        }
        return count;
    }

    void ASTCodeGen::requireEqualExtent(Operand* lhs, Operand* rhs)
    {
        if (lhs == rhs) return;
        Operand* test = getRegOperand(getNewRegId());
        insert(new IcmpInst(DataType::I64, ICmpOp::EQ, lhs, rhs, test));
        Block* ok = createBlock();
        Block* fail = createBlock();
        insert(new BrCondInst(test, getLabelOperand(ok->blockId), getLabelOperand(fail->blockId)));
        enterBlock(fail);
        insert(createCallInst(DataType::VOID, "llvm.trap"));
        insert(createBranchInst(fail->blockId));
        enterBlock(ok);
    }

    Operand* ASTCodeGen::allocateTensor(DataType type, const std::vector<int>& dims, Operand* first)
    {
        ASSERT(!dims.empty());
        if (dims[0] == -1)
        {
            ASSERT(first != nullptr);
            Operand* count = tensorElementCount(dims, first);
            const size_t reg = getNewRegId();
            auto* inst = createAllocaInst(type, reg);
            inst->dynamicCount = count;
            insert(inst);
            Operand* result = getRegOperand(reg);
            tensorExtents[result] = first;
            return result;
        }
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
        const std::function<void(Operand*)>& emit, Operand* first)
    {
        forTensorCount(tensorElementCount(dims, first), emit);
    }

    void ASTCodeGen::forTensorCount(Operand* count, const std::function<void(Operand*)>& emit)
    {
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

    void ASTCodeGen::copyTensor(DataType type, const std::vector<int>& dims, Operand* dst, Operand* src,
        const std::vector<int>& sourceDims)
    {
        Operand* first = tensorFirstExtent(dims, dst);
        Operand* sourceFirst = tensorFirstExtent(sourceDims.empty() ? dims : sourceDims, src);
        requireEqualExtent(first, sourceFirst);
        if (dst == src) return;
        forTensorElements(dims, [&](Operand* index) {
            Operand* val = tensorElementLoad(type, src, index);
            Operand* addr = tensorElementAddress(type, dst, index);
            insert(createStoreInst(type, val, addr));
        }, first);
    }

    Operand* ASTCodeGen::handleTensorUnary(FE::AST::UnaryExpr& node, Module* m)
    {
        ASSERT(node.op == FE::AST::Operator::ADD || node.op == FE::AST::Operator::SUB);
        Operand* source = evaluateExprValue(*node.expr, m);
        const DataType type = tensorElementType(node.attr.val.value.type);
        const auto& dims = node.attr.arrayDims;
        Operand* first = tensorFirstExtent(dims, source);
        Operand* result = allocateTensor(type, dims, first);
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
        }, first);
        return result;
    }

    // 矩阵乘法：A[M][N] @ B[N][P] -> C[M][P]。
    // 结果使用独立的临时张量，避免覆盖仍在参与计算的操作数。
    Operand* ASTCodeGen::handleTensorMatmul(FE::AST::BinaryExpr& node, Module* m)
    {
        const auto& aDims = node.lhs->attr.arrayDims;
        const auto& bDims = node.rhs->attr.arrayDims;
        const auto& cDims = node.attr.arrayDims;
        ASSERT(aDims.size() == 2 && bDims.size() == 2 && cDims.size() == 2);
        const int inner = aDims[1];
        const int cols = bDims[1];
        ASSERT(inner > 0 && cols > 0);
        ASSERT(cDims[0] == aDims[0] && cDims[1] == cols);
        const DataType type = tensorElementType(node.attr.val.value.type);
        ASSERT(type == DataType::I32 || type == DataType::F32);

        // 两个操作数各求值一次，嵌套张量表达式也先计算完毕。
        Operand* a = evaluateExprValue(*node.lhs, m);
        Operand* b = evaluateExprValue(*node.rhs, m);
        Operand* rows = tensorFirstExtent(aDims, a);
        requireEqualExtent(getImmeI32Operand(inner), tensorFirstExtent(bDims, b));
        Operand* c = allocateTensor(type, cDims, rows);
        const Operator mulOp = type == DataType::F32 ? Operator::FMUL : Operator::MUL;
        const Operator addOp = type == DataType::F32 ? Operator::FADD : Operator::ADD;
        Operand* zero = type == DataType::F32
            ? static_cast<Operand*>(getImmeF32Operand(0.0f))
            : static_cast<Operand*>(getImmeI32Operand(0));

        // 行主序偏移统一用 i64，防止 i32 乘法计算偏移时溢出。
        auto indexOp = [&](Operator op, Operand* lhs, Operand* rhs) -> Operand* {
            Operand* result = getRegOperand(getNewRegId());
            insert(new ArithmeticInst(op, DataType::I64, lhs, rhs, result));
            return result;
        };

        forTensorCount(rows, [&](Operand* i) {
            Operand* aRow = indexOp(Operator::MUL, i, getImmeI32Operand(inner));
            Operand* cRow = indexOp(Operator::MUL, i, getImmeI32Operand(cols));
            forTensorElements({cols}, [&](Operand* j) {
                // 每一个 C[i][j] 都独立从 0 开始累加。
                Block* pre = curBlock;
                Block* cond = createBlock();
                Block* body = createBlock();
                Block* end = createBlock();
                insert(createBranchInst(cond->blockId));

                enterBlock(cond);
                Operand* k = getRegOperand(getNewRegId());
                Operand* sum = getRegOperand(getNewRegId());
                auto* kPhi = new PhiInst(DataType::I64, k);
                auto* sumPhi = new PhiInst(type, sum);
                kPhi->addIncoming(getImmeI32Operand(0), getLabelOperand(pre->blockId));
                sumPhi->addIncoming(zero, getLabelOperand(pre->blockId));
                insert(kPhi);
                insert(sumPhi);
                Operand* test = getRegOperand(getNewRegId());
                insert(new IcmpInst(DataType::I64, ICmpOp::SLT, k, getImmeI32Operand(inner), test));
                insert(new BrCondInst(test, getLabelOperand(body->blockId), getLabelOperand(end->blockId)));

                enterBlock(body);
                Operand* aIndex = indexOp(Operator::ADD, aRow, k);
                Operand* bRow = indexOp(Operator::MUL, k, getImmeI32Operand(cols));
                Operand* bIndex = indexOp(Operator::ADD, bRow, j);
                Operand* av = tensorElementLoad(type, a, aIndex);
                Operand* bv = tensorElementLoad(type, b, bIndex);
                Operand* product = getRegOperand(getNewRegId());
                Operand* nextSum = getRegOperand(getNewRegId());
                insert(new ArithmeticInst(mulOp, type, av, bv, product));
                insert(new ArithmeticInst(addOp, type, sum, product, nextSum));
                Operand* nextK = indexOp(Operator::ADD, k, getImmeI32Operand(1));
                kPhi->addIncoming(nextK, getLabelOperand(curBlock->blockId));
                sumPhi->addIncoming(nextSum, getLabelOperand(curBlock->blockId));
                insert(createBranchInst(cond->blockId));

                // 循环结束时 sum 即完整的点积结果。
                enterBlock(end);
                Operand* cIndex = indexOp(Operator::ADD, cRow, j);
                Operand* addr = tensorElementAddress(type, c, cIndex);
                insert(createStoreInst(type, sum, addr));
            });
        });
        return c;
    }

    Operand* ASTCodeGen::handleTensorBinary(FE::AST::BinaryExpr& node, Module* m)
    {
        using AOp = FE::AST::Operator;
        if (node.op == AOp::MATMUL) return handleTensorMatmul(node, m);
        const DataType type = tensorElementType(node.attr.val.value.type);
        const auto& dims = node.attr.arrayDims;
        const bool lt = node.lhs->attr.val.value.type->getTypeGroup() == FE::AST::TypeGroup::TENSOR;
        const bool rt = node.rhs->attr.val.value.type->getTypeGroup() == FE::AST::TypeGroup::TENSOR;
        // 表达式（尤其是标量函数调用）各求值一次，不能放入元素循环中重复求值。
        Operand* left = evaluateExprValue(*node.lhs, m);
        Operand* right = evaluateExprValue(*node.rhs, m);
        if (!lt) left = convertOperand(convert(node.lhs->attr.val.value.type), type, left, curBlock);
        if (!rt) right = convertOperand(convert(node.rhs->attr.val.value.type), type, right, curBlock);
        Operand* lf = lt ? tensorFirstExtent(node.lhs->attr.arrayDims, left) : nullptr;
        Operand* rf = rt ? tensorFirstExtent(node.rhs->attr.arrayDims, right) : nullptr;
        if (lt && rt) requireEqualExtent(lf, rf);
        Operand* first = lt ? lf : rf;
        Operand* result = allocateTensor(type, dims, first);
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
        }, first);
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
            copyTensor(tensorElementType(lhs.attr.val.value.type), lhs.attr.arrayDims, ptr, value, rhs.attr.arrayDims);
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
        args.reserve(argumentCount * 2 + 1);
        const bool tensorReturn = callee->retType->getTypeGroup() == FE::AST::TypeGroup::TENSOR;
        Operand* returnBuffer = nullptr;
        if (tensorReturn)
        {
            returnBuffer = allocateTensor(tensorElementType(callee->retType), node.attr.arrayDims);
            args.emplace_back(DataType::PTR, returnBuffer);
        }
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
            if (parameter->type->getTypeGroup() == FE::AST::TypeGroup::TENSOR)
                args.emplace_back(DataType::I64, tensorFirstExtent(argument->attr.arrayDims, value));
        }

        if (tensorReturn)
        {
            insert(createCallInst(DataType::VOID, node.func->getName(), args));
            exprResult = returnBuffer;
            return;
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
