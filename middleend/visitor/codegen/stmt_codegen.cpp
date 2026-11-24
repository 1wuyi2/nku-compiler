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
        Function* function = new Function(new FuncDefInst(returnType, node.entry->getName()));
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
                    FE::AST::ParamDeclarator* param = (*node.params)[i];
                    const DataType paramType = convert(param->type);
                    const size_t ptrReg = getNewRegId();
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
        value = convertOperand(valueType, returnType, value, block);
        insert(new RetInst(returnType, value));
        exprResult = nullptr;
    }

    void ASTCodeGen::visit(FE::AST::WhileStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 while 循环 IR（条件块、循环体与结束块、循环标签）
        (void)node;
        (void)m;
        TODO("Lab3-2: Implement WhileStmt IR generation");
    }

    void ASTCodeGen::visit(FE::AST::IfStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 if/else IR（then/else/end 基本块与条件分支）
        (void)node;
        (void)m;
        TODO("Lab3-2: Implement IfStmt IR generation");
    }

    void ASTCodeGen::visit(FE::AST::BreakStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 break 的无条件跳转至循环结束块
        (void)node;
        (void)m;
        TODO("Lab3-2: Implement BreakStmt IR generation");
    }

    void ASTCodeGen::visit(FE::AST::ContinueStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 continue 的无条件跳转至循环步进/条件块
        (void)node;
        (void)m;
        TODO("Lab3-2: Implement ContinueStmt IR generation");
    }

    void ASTCodeGen::visit(FE::AST::ForStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 for 循环 IR（init/cond/body/step 基本块与循环标签）
        (void)node;
        (void)m;
        TODO("Lab3-2: Implement ForStmt IR generation");
    }
}  // namespace ME
