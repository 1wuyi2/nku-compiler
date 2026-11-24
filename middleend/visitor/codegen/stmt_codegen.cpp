#include <middleend/visitor/codegen/ast_codegen.h>

namespace ME
{
    void ASTCodeGen::visit(FE::AST::ExprStmt& node, Module* m)
    {
        if (!node.expr || isCurrentBlockTerminated()) return;
        apply(*this, *node.expr, m);
    }

    void ASTCodeGen::visit(FE::AST::FuncDeclStmt& node, Module* m)
    {
        ASSERT(m != nullptr && node.entry != nullptr && node.retType != nullptr && "Invalid function definition");
        const DataType returnType = convert(node.retType);
        ASSERT((returnType == DataType::VOID || returnType == DataType::I32 || returnType == DataType::F32) &&
               "Unsupported function return type");

        FuncDefInst::argList irParams;
        Function*            function = new Function(new FuncDefInst(returnType, node.entry->getName()));
        enterFunc(function);
        try
        {
            if (node.params)
            {
                for (FE::AST::ParamDeclarator* param : *node.params)
                {
                    ASSERT(param != nullptr && param->entry != nullptr && param->type != nullptr &&
                           "Invalid function parameter");
                    const DataType paramType = convert(param->type);
                    ASSERT((paramType == DataType::I32 || paramType == DataType::F32) &&
                           "Array parameters belong to Advanced");
                    const size_t argumentReg = getNewRegId();
                    irParams.emplace_back(paramType, getRegOperand(argumentReg));
                }
            }
            function->funcDef->argRegs = irParams;

            enterBlock(createBlock());
            if (node.params)
            {
                for (size_t i = 0; i < node.params->size(); ++i)
                {
                    FE::AST::ParamDeclarator* param     = (*node.params)[i];
                    const DataType            paramType = convert(param->type);
                    const size_t              ptrReg    = getNewRegId();
                    insert(createAllocaInst(paramType, ptrReg));
                    insert(createStoreInst(paramType, irParams[i].second, getRegOperand(ptrReg)));
                    name2reg.addSymbol(param->entry, ptrReg);
                    reg2attr[ptrReg] = FE::AST::VarAttr(param->type, false);
                }
            }

            // Parameters and declarations in the outermost function block
            // intentionally share one source-language scope.
            if (auto* body = dynamic_cast<FE::AST::BlockStmt*>(node.body))
            {
                if (body->stmts)
                    for (FE::AST::StmtNode* stmt : *body->stmts)
                    {
                        if (isCurrentBlockTerminated()) break;
                        if (stmt) apply(*this, *stmt, m);
                    }
            }
            else if (node.body && !isCurrentBlockTerminated())
                apply(*this, *node.body, m);

            if (returnType == DataType::VOID && !isCurrentBlockTerminated()) insert(createRetInst());
            ASSERT(isCurrentBlockTerminated() && "Non-void function ended without a return terminator");
        } catch (...)
        {
            delete function;
            exitFunc();
            throw;
        }
        exitFunc();
        m->functions.push_back(function);
    }

    void ASTCodeGen::visit(FE::AST::VarDeclStmt& node, Module* m)
    {
        ASSERT(node.decl != nullptr && "Variable declaration statement has no declaration");
        apply(*this, *node.decl, m);
    }

    void ASTCodeGen::visit(FE::AST::BlockStmt& node, Module* m)
    {
        name2reg.enterScope();
        try
        {
            if (node.stmts)
                for (FE::AST::StmtNode* stmt : *node.stmts)
                {
                    if (isCurrentBlockTerminated()) break;
                    if (stmt) apply(*this, *stmt, m);
                }
        } catch (...)
        {
            name2reg.exitScope();
            throw;
        }
        name2reg.exitScope();
    }

    void ASTCodeGen::visit(FE::AST::ReturnStmt& node, Module* m)
    {
        ASSERT(curFunc != nullptr && curBlock != nullptr && curFunc->funcDef != nullptr &&
               "Return requires a current function/block");
        if (isCurrentBlockTerminated()) return;

        const DataType returnType = curFunc->funcDef->retType;
        if (!node.retExpr)
        {
            ASSERT(returnType == DataType::VOID && "Non-void return requires a value");
            insert(createRetInst());
            exprResult = nullptr;
            return;
        }

        ASSERT(returnType != DataType::VOID && "Void function cannot return a value");
        Operand* value = evaluateExprValue(*node.retExpr, m);
        Block*   block = curBlock;
        ASSERT(block != nullptr && !isCurrentBlockTerminated() && "Return expression terminated its block");
        const DataType valueType = convert(node.retExpr->attr.val.value.type);
        value                    = convertOperand(valueType, returnType, value, block);
        insert(new RetInst(returnType, value));
        exprResult = nullptr;
    }

    void ASTCodeGen::visit(FE::AST::WhileStmt& node, Module* m)
    {
        ASSERT(node.cond != nullptr && "Invalid while statement");
        Block* condBlock = createBlock();
        Block* bodyBlock = createBlock();
        Block* endBlock  = createBlock();
        insert(createBranchInst(condBlock->blockId));

        enterBlock(condBlock);
        Operand* cond = evaluateCondition(*node.cond, m);
        insert(new BrCondInst(cond, getLabelOperand(bodyBlock->blockId), getLabelOperand(endBlock->blockId)));

        loopTargets.emplace_back(condBlock->blockId, endBlock->blockId);
        try
        {
            enterBlock(bodyBlock);
            if (node.body) apply(*this, *node.body, m);
            if (!isCurrentBlockTerminated()) insert(createBranchInst(condBlock->blockId));
        } catch (...)
        {
            loopTargets.pop_back();
            throw;
        }
        loopTargets.pop_back();
        enterBlock(endBlock);
    }

    void ASTCodeGen::visit(FE::AST::IfStmt& node, Module* m)
    {
        ASSERT(node.cond != nullptr && "Invalid if statement");
        Operand* cond      = evaluateCondition(*node.cond, m);
        Block*   thenBlock = createBlock();

        if (!node.elseStmt)
        {
            Block* mergeBlock = createBlock();
            insert(new BrCondInst(cond, getLabelOperand(thenBlock->blockId), getLabelOperand(mergeBlock->blockId)));
            enterBlock(thenBlock);
            if (node.thenStmt) apply(*this, *node.thenStmt, m);
            if (!isCurrentBlockTerminated()) insert(createBranchInst(mergeBlock->blockId));
            enterBlock(mergeBlock);
            return;
        }

        Block* elseBlock = createBlock();
        insert(new BrCondInst(cond, getLabelOperand(thenBlock->blockId), getLabelOperand(elseBlock->blockId)));

        enterBlock(thenBlock);
        if (node.thenStmt) apply(*this, *node.thenStmt, m);
        Block*     thenEnd        = curBlock;
        const bool thenTerminated = isCurrentBlockTerminated();

        enterBlock(elseBlock);
        apply(*this, *node.elseStmt, m);
        Block*     elseEnd        = curBlock;
        const bool elseTerminated = isCurrentBlockTerminated();

        if (thenTerminated && elseTerminated)
        {
            enterBlock(elseEnd);
            return;
        }

        Block* mergeBlock = createBlock();
        if (!thenTerminated)
        {
            enterBlock(thenEnd);
            insert(createBranchInst(mergeBlock->blockId));
        }
        if (!elseTerminated)
        {
            enterBlock(elseEnd);
            insert(createBranchInst(mergeBlock->blockId));
        }
        enterBlock(mergeBlock);
    }

    void ASTCodeGen::visit(FE::AST::BreakStmt& node, Module* m)
    {
        (void)node;
        (void)m;
        ASSERT(!loopTargets.empty() && "break statement is not within a loop");
        insert(createBranchInst(loopTargets.back().second));
        exprResult = nullptr;
    }

    void ASTCodeGen::visit(FE::AST::ContinueStmt& node, Module* m)
    {
        (void)node;
        (void)m;
        ASSERT(!loopTargets.empty() && "continue statement is not within a loop");
        insert(createBranchInst(loopTargets.back().first));
        exprResult = nullptr;
    }

    void ASTCodeGen::visit(FE::AST::ForStmt& node, Module* m)
    {
        name2reg.enterScope();
        try
        {
            if (node.init) apply(*this, *node.init, m);

            Block* condBlock = createBlock();
            Block* bodyBlock = createBlock();
            Block* stepBlock = createBlock();
            Block* endBlock  = createBlock();
            insert(createBranchInst(condBlock->blockId));

            enterBlock(condBlock);
            if (node.cond)
            {
                Operand* cond = evaluateCondition(*node.cond, m);
                insert(new BrCondInst(cond, getLabelOperand(bodyBlock->blockId), getLabelOperand(endBlock->blockId)));
            }
            else
                insert(createBranchInst(bodyBlock->blockId));

            loopTargets.emplace_back(stepBlock->blockId, endBlock->blockId);
            try
            {
                enterBlock(bodyBlock);
                if (node.body) apply(*this, *node.body, m);
                if (!isCurrentBlockTerminated()) insert(createBranchInst(stepBlock->blockId));

                enterBlock(stepBlock);
                if (node.step) apply(*this, *node.step, m);
                if (!isCurrentBlockTerminated()) insert(createBranchInst(condBlock->blockId));
            } catch (...)
            {
                loopTargets.pop_back();
                throw;
            }
            loopTargets.pop_back();
            enterBlock(endBlock);
        } catch (...)
        {
            name2reg.exitScope();
            throw;
        }
        name2reg.exitScope();
    }
}  // namespace ME
