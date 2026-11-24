#include <middleend/visitor/codegen/ast_codegen.h>

namespace ME
{
    namespace
    {
        std::array<DataType, FE::AST::maxTypeIdx + 1> at2dt = []() {
            std::array<DataType, FE::AST::maxTypeIdx + 1> ret{};
            ret.fill(DataType::UNK);
            ret[static_cast<size_t>(FE::AST::Type_t::UNK)]   = DataType::UNK;
            ret[static_cast<size_t>(FE::AST::Type_t::VOID)]  = DataType::VOID;
            ret[static_cast<size_t>(FE::AST::Type_t::BOOL)]  = DataType::I1;
            ret[static_cast<size_t>(FE::AST::Type_t::INT)]   = DataType::I32;
            ret[static_cast<size_t>(FE::AST::Type_t::LL)]    = DataType::I32;  // treat as I32
            ret[static_cast<size_t>(FE::AST::Type_t::FLOAT)] = DataType::F32;
            return ret;
        }();

        bool isImmediate(Operand* operand)
        {
            return operand->getType() == OperandType::IMMEI32 || operand->getType() == OperandType::IMMEF32;
        }

        DataType promoteType(DataType lhs, DataType rhs)
        {
            if (lhs == DataType::F32 || rhs == DataType::F32) return DataType::F32;
            return DataType::I32;  // SysY integer promotion includes bool -> int.
        }

        bool isComparison(FE::AST::Operator op)
        {
            return op == FE::AST::Operator::GT || op == FE::AST::Operator::GE || op == FE::AST::Operator::LT ||
                   op == FE::AST::Operator::LE || op == FE::AST::Operator::EQ || op == FE::AST::Operator::NEQ;
        }

        Operator integerOpcode(FE::AST::Operator op)
        {
            switch (op)
            {
                case FE::AST::Operator::ADD: return Operator::ADD;
                case FE::AST::Operator::SUB: return Operator::SUB;
                case FE::AST::Operator::MUL: return Operator::MUL;
                case FE::AST::Operator::DIV: return Operator::DIV;
                case FE::AST::Operator::MOD: return Operator::MOD;
                default: ERROR("Unsupported integer arithmetic operator");
            }
        }

        Operator floatOpcode(FE::AST::Operator op)
        {
            switch (op)
            {
                case FE::AST::Operator::ADD: return Operator::FADD;
                case FE::AST::Operator::SUB: return Operator::FSUB;
                case FE::AST::Operator::MUL: return Operator::FMUL;
                case FE::AST::Operator::DIV: return Operator::FDIV;
                default: ERROR("Unsupported floating-point arithmetic operator");
            }
        }

        ICmpOp integerPredicate(FE::AST::Operator op)
        {
            switch (op)
            {
                case FE::AST::Operator::GT: return ICmpOp::SGT;
                case FE::AST::Operator::GE: return ICmpOp::SGE;
                case FE::AST::Operator::LT: return ICmpOp::SLT;
                case FE::AST::Operator::LE: return ICmpOp::SLE;
                case FE::AST::Operator::EQ: return ICmpOp::EQ;
                case FE::AST::Operator::NEQ: return ICmpOp::NE;
                default: ERROR("Unsupported integer comparison operator");
            }
        }

        FCmpOp floatPredicate(FE::AST::Operator op)
        {
            switch (op)
            {
                case FE::AST::Operator::GT: return FCmpOp::OGT;
                case FE::AST::Operator::GE: return FCmpOp::OGE;
                case FE::AST::Operator::LT: return FCmpOp::OLT;
                case FE::AST::Operator::LE: return FCmpOp::OLE;
                case FE::AST::Operator::EQ: return FCmpOp::OEQ;
                case FE::AST::Operator::NEQ: return FCmpOp::ONE;
                default: ERROR("Unsupported floating-point comparison operator");
            }
        }
    }  // namespace

    DataType ASTCodeGen::convert(FE::AST::Type* at)
    {
        if (!at) return DataType::UNK;
        if (at->getTypeGroup() == FE::AST::TypeGroup::POINTER) return DataType::PTR;
        return at2dt[static_cast<size_t>(at->getBaseType())];
    }

    Operand* ASTCodeGen::evaluateExprValue(FE::AST::ExprNode& node, Module* m)
    {
        exprResult = nullptr;
        apply(*this, node, m);
        ASSERT(exprResult != nullptr && "Expression did not produce an IR value");
        return exprResult;
    }

    Operand* ASTCodeGen::convertOperand(DataType from, DataType to, Operand* src, Block* block)
    {
        ASSERT(src != nullptr && block != nullptr && "Type conversion requires an operand and block");
        if (from == to) return src;
        ASSERT((from == DataType::I1) || (from == DataType::I32) || (from == DataType::F32));
        ASSERT((to == DataType::I1) || (to == DataType::I32) || (to == DataType::F32));

        if (isImmediate(src))
        {
            if (src->getType() == OperandType::IMMEI32)
            {
                const int value = static_cast<ImmeI32Operand*>(src)->value;
                if (to == DataType::I1) return getImmeI32Operand(value != 0);
                if (to == DataType::I32) return getImmeI32Operand(value);
                return getImmeF32Operand(static_cast<float>(value));
            }
            const float value = static_cast<ImmeF32Operand*>(src)->value;
            if (to == DataType::I1) return getImmeI32Operand(value != 0.0f);
            if (to == DataType::I32) return getImmeI32Operand(static_cast<int>(value));
            return getImmeF32Operand(value);
        }

        ASSERT(src->getType() == OperandType::REG && "Only immediates and registers are scalar values");
        const size_t srcReg  = src->getRegNum();
        size_t       destReg = getNewRegId();

        if (from == DataType::I1 && to == DataType::I32)
            block->insert(createZextInst(srcReg, destReg, 1, 32));
        else if (from == DataType::I1 && to == DataType::F32)
        {
            block->insert(createZextInst(srcReg, destReg, 1, 32));
            const size_t i32Reg = destReg;
            destReg             = getNewRegId();
            block->insert(createSI2FPInst(i32Reg, destReg));
        }
        else if (from == DataType::I32 && to == DataType::I1)
            block->insert(new IcmpInst(DataType::I32, ICmpOp::NE, src, getImmeI32Operand(0), getRegOperand(destReg)));
        else if (from == DataType::I32 && to == DataType::F32)
            block->insert(createSI2FPInst(srcReg, destReg));
        else if (from == DataType::F32 && to == DataType::I1)
            block->insert(
                new FcmpInst(DataType::F32, FCmpOp::ONE, src, getImmeF32Operand(0.0f), getRegOperand(destReg)));
        else if (from == DataType::F32 && to == DataType::I32)
            block->insert(createFP2SIInst(srcReg, destReg));
        else
            ERROR("Type conversion not supported");

        return getRegOperand(destReg);
    }

    Operand* ASTCodeGen::handleUnaryCalc(FE::AST::UnaryExpr& node, Module* m)
    {
        Operand* src   = evaluateExprValue(*node.expr, m);
        Block*   block = curBlock;
        ASSERT(block != nullptr && "Unary expression requires a current block after operand evaluation");

        const DataType srcType = convert(node.expr->attr.val.value.type);
        ASSERT(srcType == DataType::I1 || srcType == DataType::I32 || srcType == DataType::F32);

        if (node.op == FE::AST::Operator::NOT)
        {
            if (isImmediate(src))
            {
                if (src->getType() == OperandType::IMMEF32)
                    return getImmeI32Operand(static_cast<ImmeF32Operand*>(src)->value == 0.0f);
                return getImmeI32Operand(static_cast<ImmeI32Operand*>(src)->value == 0);
            }

            const size_t dest = getNewRegId();
            if (srcType == DataType::F32)
                block->insert(
                    new FcmpInst(DataType::F32, FCmpOp::OEQ, src, getImmeF32Operand(0.0f), getRegOperand(dest)));
            else
                block->insert(new IcmpInst(srcType, ICmpOp::EQ, src, getImmeI32Operand(0), getRegOperand(dest)));
            return getRegOperand(dest);
        }

        if (node.op != FE::AST::Operator::ADD && node.op != FE::AST::Operator::SUB)
            ERROR("Unsupported unary operator");

        const DataType resultType = convert(node.attr.val.value.type);
        src                       = convertOperand(srcType, resultType, src, block);
        if (node.op == FE::AST::Operator::ADD) return src;

        const size_t dest = getNewRegId();
        if (resultType == DataType::I32)
            block->insert(
                new ArithmeticInst(Operator::SUB, DataType::I32, getImmeI32Operand(0), src, getRegOperand(dest)));
        else if (resultType == DataType::F32)
            block->insert(
                new ArithmeticInst(Operator::FSUB, DataType::F32, getImmeF32Operand(0.0f), src, getRegOperand(dest)));
        else
            ERROR("Unsupported unary arithmetic type");
        return getRegOperand(dest);
    }

    Operand* ASTCodeGen::handleBinaryCalc(
        FE::AST::ExprNode& lhs, FE::AST::ExprNode& rhs, FE::AST::Operator bop, Module* m)
    {
        Operand* lhsValue = evaluateExprValue(lhs, m);
        Operand* rhsValue = evaluateExprValue(rhs, m);
        Block*   block    = curBlock;
        ASSERT(block != nullptr && "Binary expression requires a current block after operand evaluation");

        const DataType lhsType = convert(lhs.attr.val.value.type);
        const DataType rhsType = convert(rhs.attr.val.value.type);
        ASSERT(lhsType == DataType::I1 || lhsType == DataType::I32 || lhsType == DataType::F32);
        ASSERT(rhsType == DataType::I1 || rhsType == DataType::I32 || rhsType == DataType::F32);

        const DataType commonType = promoteType(lhsType, rhsType);
        lhsValue                  = convertOperand(lhsType, commonType, lhsValue, block);
        rhsValue                  = convertOperand(rhsType, commonType, rhsValue, block);

        const size_t dest = getNewRegId();
        if (isComparison(bop))
        {
            if (commonType == DataType::F32)
                block->insert(
                    new FcmpInst(DataType::F32, floatPredicate(bop), lhsValue, rhsValue, getRegOperand(dest)));
            else
                block->insert(
                    new IcmpInst(DataType::I32, integerPredicate(bop), lhsValue, rhsValue, getRegOperand(dest)));
        }
        else if (commonType == DataType::I32)
            block->insert(
                new ArithmeticInst(integerOpcode(bop), DataType::I32, lhsValue, rhsValue, getRegOperand(dest)));
        else if (commonType == DataType::F32)
            block->insert(new ArithmeticInst(floatOpcode(bop), DataType::F32, lhsValue, rhsValue, getRegOperand(dest)));
        else
            ERROR("Unsupported binary operand type");

        return getRegOperand(dest);
    }
}  // namespace ME
