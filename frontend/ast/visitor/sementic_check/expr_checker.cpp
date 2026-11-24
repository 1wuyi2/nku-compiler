#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>

namespace FE::AST
{
    namespace
    {
        bool isScalar(Type* type)
        {
            if (!type || type->getTypeGroup() != TypeGroup::BASIC) return false;
            const Type_t kind = type->getBaseType();
            return kind == Type_t::BOOL || kind == Type_t::INT || kind == Type_t::LL || kind == Type_t::FLOAT;
        }

        bool isIntegral(Type* type)
        {
            if (!isScalar(type)) return false;
            const Type_t kind = type->getBaseType();
            return kind == Type_t::BOOL || kind == Type_t::INT || kind == Type_t::LL;
        }

        bool isUnaryOperator(Operator op) { return op == Operator::ADD || op == Operator::SUB || op == Operator::NOT; }

        bool isBinaryOperator(Operator op)
        {
            switch (op)
            {
                case Operator::ADD:
                case Operator::SUB:
                case Operator::MUL:
                case Operator::DIV:
                case Operator::MOD:
                case Operator::GT:
                case Operator::GE:
                case Operator::LT:
                case Operator::LE:
                case Operator::EQ:
                case Operator::NEQ:
                case Operator::AND:
                case Operator::OR: return true;
                default: return false;
            }
        }
    }  // namespace

    bool ASTChecker::visit(LeftValExpr& node)
    {
        // TODO(Lab3-1): 实现左值表达式的语义检查
        // 检查变量是否存在，处理数组下标访问，进行类型检查和常量折叠
        VarAttr* attr = symTable.getSymbol(node.entry);
        if (!attr)
        {
            const std::string name = node.entry ? node.entry->getName() : "<null>";
            reportError(node, "use of undeclared variable '" + name + "'");
            node.isLval               = false;
            node.attr.val.value.type  = voidType;
            node.attr.val.isConstexpr = false;
            return false;
        }

        bool result = true;
        if (node.indices)
        {
            for (auto* index : *node.indices)
                if (index) result &= apply(*this, *index);
        }

        node.isLval               = true;
        node.attr.val.value.type  = attr->type;
        node.attr.val.isConstexpr = false;
        if (attr->isConstDecl && !attr->initList.empty())
        {
            node.attr.val.value       = attr->initList.front();
            node.attr.val.isConstexpr = evaluateConstants;
        }
        return result;
    }

    bool ASTChecker::visit(LiteralExpr& node)
    {
        // 示例实现：字面量表达式的语义检查
        // 字面量总是编译期常量，直接设置属性
        node.attr.val.isConstexpr = evaluateConstants;
        node.attr.val.value       = node.literal;
        return true;
    }

    bool ASTChecker::visit(UnaryExpr& node)
    {
        // TODO(Lab3-1): 实现一元表达式的语义检查
        // 访问子表达式，检查操作数类型，调用类型推断函数
        node.attr.op              = node.op;
        node.attr.val.value.type  = voidType;
        node.attr.val.isConstexpr = false;
        if (!node.expr)
        {
            reportError(node, "missing operand for unary operator '" + toString(node.op) + "'");
            return false;
        }

        const bool childOk = apply(*this, *node.expr);
        Type*      type    = node.expr->attr.val.value.type;
        if (!childOk || !isScalar(type))
        {
            if (childOk)
                reportError(node,
                    "invalid operand type '" + (type ? type->toString() : std::string("<null>")) +
                        "' for unary operator '" + toString(node.op) + "'");
            return false;
        }
        if (!isUnaryOperator(node.op))
        {
            reportError(node, "invalid unary operator '" + toString(node.op) + "'");
            return false;
        }

        bool hasError = false;
        node.attr.val = typeInfer(node.expr->attr.val, node.op, node, hasError);
        if (!evaluateConstants) node.attr.val.isConstexpr = false;
        return !hasError;
    }

    bool ASTChecker::visit(BinaryExpr& node)
    {
        // TODO(Lab3-1): 实现二元表达式的语义检查
        // 访问左右子表达式，检查操作数类型，调用类型推断
        node.attr.op              = node.op;
        node.attr.val.value.type  = voidType;
        node.attr.val.isConstexpr = false;
        if (!node.lhs || !node.rhs)
        {
            reportError(node, "missing operand for binary operator '" + toString(node.op) + "'");
            return false;
        }

        const bool lhsOk = apply(*this, *node.lhs);

        bool skipRhsEvaluation = false;
        if ((node.op == Operator::AND || node.op == Operator::OR) && lhsOk && node.lhs->attr.val.isConstexpr &&
            isScalar(node.lhs->attr.val.value.type))
        {
            const bool lhsValue = node.lhs->attr.val.getBool();
            skipRhsEvaluation   = (node.op == Operator::AND && !lhsValue) || (node.op == Operator::OR && lhsValue);
        }

        const bool oldEvaluateConstants = evaluateConstants;
        if (skipRhsEvaluation) evaluateConstants = false;
        bool rhsOk = false;
        try
        {
            rhsOk = apply(*this, *node.rhs);
        } catch (...)
        {
            evaluateConstants = oldEvaluateConstants;
            throw;
        }
        evaluateConstants = oldEvaluateConstants;

        Type* lhsType = node.lhs->attr.val.value.type;
        Type* rhsType = node.rhs->attr.val.value.type;
        if (!lhsOk || !rhsOk) return false;

        if (node.op == Operator::ASSIGN)
        {
            auto* lval = dynamic_cast<LeftValExpr*>(node.lhs);
            if (!lval || !lval->isLval || !lval->entry)
            {
                reportError(node, "left operand of assignment is not a modifiable lvalue");
                return false;
            }
            VarAttr* var = symTable.getSymbol(lval->entry);
            if (!var)
            {
                // LeftValExpr already reports this case; keep the expression invalid.
                return false;
            }
            if (var->isConstDecl)
            {
                reportError(node, "assignment to const variable '" + lval->entry->getName() + "'");
                return false;
            }
            if (!isScalar(lhsType) || !isScalar(rhsType))
            {
                reportError(node, "incompatible operand types for assignment");
                return false;
            }
            node.attr.val.value.type  = lhsType;
            node.attr.val.isConstexpr = false;
            return true;
        }

        if (!isBinaryOperator(node.op))
        {
            reportError(node, "invalid binary operator '" + toString(node.op) + "'");
            return false;
        }
        if (!isScalar(lhsType) || !isScalar(rhsType))
        {
            reportError(node, "invalid operand type for binary operator '" + toString(node.op) + "'");
            return false;
        }
        if (node.op == Operator::MOD && (!isIntegral(lhsType) || !isIntegral(rhsType)))
        {
            reportError(node, "operator '%' requires integer operands");
            return false;
        }

        if (skipRhsEvaluation)
        {
            node.attr.val = ExprValue(VarValue(node.op == Operator::OR), true);
            return true;
        }

        bool hasError = false;
        node.attr.val = typeInfer(node.lhs->attr.val, node.rhs->attr.val, node.op, node, hasError);
        if (!evaluateConstants) node.attr.val.isConstexpr = false;
        return !hasError;
    }

    bool ASTChecker::visit(CallExpr& node)
    {
        // TODO(Lab3-1): 实现函数调用表达式的语义检查
        // 检查函数是否存在，访问实参列表，检查参数数量和类型匹配
        node.attr.val.value.type  = voidType;
        node.attr.val.isConstexpr = false;

        bool result = true;
        if (node.args)
        {
            for (auto* arg : *node.args)
            {
                if (!arg)
                {
                    reportError(node, "null argument in function call");
                    result = false;
                    continue;
                }
                result &= apply(*this, *arg);
            }
        }

        const std::string name = node.func ? node.func->getName() : "<null>";
        if (!node.func)
        {
            reportError(node, "call has no function name");
            return false;
        }
        if (symTable.getSymbol(node.func))
        {
            reportError(node, "called object '" + name + "' is not a function");
            return false;
        }

        auto found = funcDecls.find(node.func);
        if (found == funcDecls.end() || !found->second)
        {
            reportError(node, "call to undeclared function '" + name + "'");
            return false;
        }

        FuncDeclStmt* function       = found->second;
        const size_t  argumentCount  = node.args ? node.args->size() : 0;
        const size_t  parameterCount = function->params ? function->params->size() : 0;
        if (argumentCount != parameterCount)
        {
            reportError(node,
                "function '" + name + "' expects " + std::to_string(parameterCount) + " argument(s), but " +
                    std::to_string(argumentCount) + " were provided");
            result = false;
        }

        const size_t commonCount = std::min(argumentCount, parameterCount);
        for (size_t i = 0; i < commonCount; ++i)
        {
            ExprNode*        argument  = (*node.args)[i];
            ParamDeclarator* parameter = (*function->params)[i];
            if (!argument || !parameter) continue;

            Type*      argumentType  = argument->attr.val.value.type;
            Type*      parameterType = parameter->type;
            const bool bothScalar    = isScalar(argumentType) && isScalar(parameterType);
            const bool exactType     = argumentType && parameterType &&
                                   argumentType->getTypeGroup() == parameterType->getTypeGroup() &&
                                   argumentType->getBaseType() == parameterType->getBaseType();
            if (!bothScalar && !exactType)
            {
                reportError(node,
                    "argument " + std::to_string(i + 1) + " of function '" + name + "' has type '" +
                        (argumentType ? argumentType->toString() : std::string("<null>")) +
                        "', but parameter type is '" +
                        (parameterType ? parameterType->toString() : std::string("<null>")) + "'");
                result = false;
            }
        }

        node.attr.val.value.type = function->retType ? function->retType : voidType;
        return result;
    }

    bool ASTChecker::visit(CommaExpr& node)
    {
        // TODO(Lab3-1): 实现逗号表达式的语义检查
        // 依序访问各子表达式，结果为最后一个表达式的属性
        node.attr.val.value.type  = voidType;
        node.attr.val.isConstexpr = false;
        if (!node.exprs || node.exprs->empty())
        {
            reportError(node, "empty comma expression");
            return false;
        }

        bool result       = true;
        bool allConstexpr = true;
        bool found        = false;
        for (auto* expr : *node.exprs)
        {
            if (!expr)
            {
                reportError(node, "null expression in comma expression");
                result       = false;
                allConstexpr = false;
                continue;
            }
            const bool ok = apply(*this, *expr);
            result &= ok;
            allConstexpr &= ok && expr->attr.val.isConstexpr;
            node.attr = expr->attr;
            found     = true;
        }
        if (!found) return false;
        node.attr.val.isConstexpr = result && allConstexpr;
        return result;
    }
}  // namespace FE::AST
