#include <middleend/visitor/codegen/ast_codegen.h>
#include <debug.h>
#include <limits>

namespace ME
{
    void ASTCodeGen::visit(FE::AST::Initializer& node, Module* m)
    {
        (void)m;
        ERROR("Initializer should not appear here, at line %d", node.line_num);
    }
    void ASTCodeGen::visit(FE::AST::InitializerList& node, Module* m)
    {
        (void)m;
        ERROR("InitializerList should not appear here, at line %d", node.line_num);
    }
    void ASTCodeGen::visit(FE::AST::VarDeclarator& node, Module* m)
    {
        (void)m;
        ERROR("VarDeclarator should not appear here, at line %d", node.line_num);
    }
    void ASTCodeGen::visit(FE::AST::ParamDeclarator& node, Module* m)
    {
        (void)m;
        ERROR("ParamDeclarator should not appear here, at line %d", node.line_num);
    }

    void ASTCodeGen::visit(FE::AST::VarDeclaration& node, Module* m)
    {
        ASSERT(curFunc != nullptr && curBlock != nullptr && "Local declaration requires a current function/block");
        if (!node.decls) return;

        const DataType type = tensorElementType(node.type);
        ASSERT((type == DataType::I32 || type == DataType::F32) && "Unsupported local scalar type");

        for (FE::AST::VarDeclarator* declarator : *node.decls)
        {
            if (!declarator) continue;
            auto* lval = dynamic_cast<FE::AST::LeftValExpr*>(declarator->lval);
            ASSERT(lval != nullptr && lval->entry != nullptr && "Invalid local variable declarator");
            // 维度表达式已在语义分析阶段计算为正整数常量。
            std::vector<int> dims;
            size_t elementCount = 1;
            if (lval->indices)
                for (auto* dim : *lval->indices)
                {
                    ASSERT(dim && dim->attr.val.isConstexpr);
                    const int extent = dim->attr.val.getInt();
                    ASSERT(extent > 0);
                    dims.push_back(extent);
                    elementCount *= static_cast<size_t>(extent);
                }

            const size_t ptrReg = getNewRegId();
            // Keep stack allocation out of loop bodies.  The initializer must
            // still execute at the declaration point, but the storage itself
            // has function lifetime and can be allocated in the entry block.
            Block* entryBlock = curFunc->getBlock(0);
            ASSERT(entryBlock != nullptr && "Function entry block is missing");
            entryBlock->insertFront(createAllocaInst(type, ptrReg, dims));
            name2reg.addSymbol(lval->entry, ptrReg);
            reg2attr[ptrReg] = FE::AST::VarAttr(node.type, node.isConstDecl);
            reg2attr[ptrReg].arrayDims = dims;
            lval2ptr[lval]   = getRegOperand(ptrReg);

            if (!declarator->init) continue;
            if (!dims.empty())
            {
                if (node.type->getTypeGroup() == FE::AST::TypeGroup::TENSOR && declarator->init->singleInit)
                {
                    auto* initializer = dynamic_cast<FE::AST::Initializer*>(declarator->init);
                    ASSERT(initializer && initializer->init_val);
                    Operand* source = evaluateExprValue(*initializer->init_val, m);
                    copyTensor(type, dims, getRegOperand(ptrReg), source);
                    continue;
                }
                // 将行主序偏移还原成各维下标，首个 0 表示同一个数组对象。
                auto addressAt = [&](size_t offset) -> Operand* {
                    std::vector<Operand*> indices(dims.size() + 1, getImmeI32Operand(0));
                    for (size_t i = dims.size(); i > 0; --i)
                    {
                        const size_t extent = static_cast<size_t>(dims[i - 1]);
                        indices[i] = getImmeI32Operand(static_cast<int>(offset % extent));
                        offset /= extent;
                    }
                    ASSERT(offset == 0);
                    const size_t addrReg = getNewRegId();
                    insert(createGEP_I32Inst(type, getRegOperand(ptrReg), dims, indices, addrReg));
                    return getRegOperand(addrReg);
                };

                // 显式初始化时先清零整个数组，再按源程序顺序写入显式元素。
                // 复用已有的 i32 长度 memset；大数组按块处理，避免长度溢出。
                constexpr size_t maxChunk = static_cast<size_t>(std::numeric_limits<int>::max()) / 4;
                for (size_t offset = 0; offset < elementCount;)
                {
                    const size_t remaining = elementCount - offset;
                    const size_t count = remaining < maxChunk ? remaining : maxChunk;
                    Operand* ptr = offset == 0 ? getRegOperand(ptrReg) : addressAt(offset);
                    CallInst::argList args = {{DataType::PTR, ptr},
                        {DataType::I8, getImmeI32Operand(0)},
                        {DataType::I32, getImmeI32Operand(static_cast<int>(count * 4))},
                        {DataType::I1, getImmeI32Operand(0)}};
                    insert(createCallInst(DataType::VOID, "llvm.memset.p0.i32", args));
                    offset += count;
                }

                for (const auto& [offset, expr] : declarator->arrayInitEntries)
                {
                    ASSERT(expr != nullptr && offset < elementCount);
                    Operand* value = nullptr;
                    if (expr->attr.val.isConstexpr)
                    {
                        // 常量已通过语义检查，直接生成目标元素类型的立即数。
                        if (type == DataType::F32)
                            value = getImmeF32Operand(expr->attr.val.getFloat());
                        else
                            value = getImmeI32Operand(expr->attr.val.getInt());
                    }
                    else
                    {
                        value = evaluateExprValue(*expr, m);
                        value = convertOperand(convert(expr->attr.val.value.type), type, value, curBlock);
                    }
                    Operand* address = addressAt(offset);
                    insert(createStoreInst(type, value, address));
                }
                continue;
            }
            ASSERT(declarator->init->singleInit && "Initializer lists for arrays belong to Advanced");
            auto* initializer = dynamic_cast<FE::AST::Initializer*>(declarator->init);
            ASSERT(initializer != nullptr && initializer->init_val != nullptr && "Invalid scalar initializer");

            Operand* value = evaluateExprValue(*initializer->init_val, m);
            Block*   block = curBlock;
            ASSERT(block != nullptr && "Initializer expression did not leave a current block");
            const DataType valueType = convert(initializer->init_val->attr.val.value.type);
            value                    = convertOperand(valueType, type, value, block);
            block->insert(createStoreInst(type, value, getRegOperand(ptrReg)));
        }
    }
}  // namespace ME
