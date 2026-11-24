#include <middleend/visitor/codegen/ast_codegen.h>
#include <debug.h>

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

        const DataType type = convert(node.type);
        ASSERT((type == DataType::I32 || type == DataType::F32) && "Unsupported local scalar type");

        for (FE::AST::VarDeclarator* declarator : *node.decls)
        {
            if (!declarator) continue;
            auto* lval = dynamic_cast<FE::AST::LeftValExpr*>(declarator->lval);
            ASSERT(lval != nullptr && lval->entry != nullptr && "Invalid local variable declarator");
            ASSERT((!lval->indices || lval->indices->empty()) && "Array code generation belongs to Advanced");

            const size_t ptrReg = getNewRegId();
            // Keep stack allocation out of loop bodies.  The initializer must
            // still execute at the declaration point, but the storage itself
            // has function lifetime and can be allocated in the entry block.
            Block* entryBlock = curFunc->getBlock(0);
            ASSERT(entryBlock != nullptr && "Function entry block is missing");
            entryBlock->insertFront(createAllocaInst(type, ptrReg));
            name2reg.addSymbol(lval->entry, ptrReg);
            reg2attr[ptrReg] = FE::AST::VarAttr(node.type, node.isConstDecl);
            lval2ptr[lval]   = getRegOperand(ptrReg);

            if (!declarator->init) continue;
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
