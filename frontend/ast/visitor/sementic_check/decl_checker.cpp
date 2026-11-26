#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>
#include <cmath>
#include <limits>
#include <new>

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

        bool isTensor(Type* type)
        {
            return type && type->getTypeGroup() == TypeGroup::TENSOR;
        }

        bool isValidVariableType(Type* type)
        {
            if (!type || (type->getTypeGroup() != TypeGroup::BASIC && !isTensor(type))) return false;
            const Type_t kind = type->getBaseType();
            return kind == Type_t::INT || kind == Type_t::FLOAT;
        }

        bool convertConstant(const VarValue& source, Type* target, VarValue& result)
        {
            if (!isScalar(source.type) || !isScalar(target)) return false;

            switch (target->getBaseType())
            {
                case Type_t::BOOL: result = VarValue(source.getBool()); return true;
                case Type_t::INT:
                {
                    long double value = 0;
                    switch (source.type->getBaseType())
                    {
                        case Type_t::BOOL: value = source.boolValue; break;
                        case Type_t::INT: value = source.intValue; break;
                        case Type_t::LL:
                        {
                            // SysY hexadecimal integer constants such as
                            // 0x80000000 denote their 32-bit bit pattern when
                            // used as int values.
                            const long long llValue = source.llValue;
                            if (llValue >= std::numeric_limits<int>::min() &&
                                llValue <= std::numeric_limits<int>::max())
                            {
                                result = VarValue(static_cast<int>(llValue));
                                return true;
                            }
                            if (llValue < 0 || static_cast<unsigned long long>(llValue) >
                                                   std::numeric_limits<unsigned int>::max())
                                return false;
                            const unsigned long long bits = static_cast<unsigned long long>(llValue);
                            const long long signedValue =
                                bits <= static_cast<unsigned long long>(std::numeric_limits<int>::max())
                                    ? static_cast<long long>(bits)
                                    : static_cast<long long>(bits) - (1LL << 32);
                            result = VarValue(static_cast<int>(signedValue));
                            return true;
                        }
                        case Type_t::FLOAT: value = source.floatValue; break;
                        default: return false;
                    }
                    if (!std::isfinite(value) || value < std::numeric_limits<int>::min() ||
                        value > std::numeric_limits<int>::max())
                        return false;
                    result = VarValue(static_cast<int>(value));
                    return true;
                }
                case Type_t::LL:
                {
                    long double value = 0;
                    switch (source.type->getBaseType())
                    {
                        case Type_t::BOOL: value = source.boolValue; break;
                        case Type_t::INT: value = source.intValue; break;
                        case Type_t::LL: value = source.llValue; break;
                        case Type_t::FLOAT: value = source.floatValue; break;
                        default: return false;
                    }
                    if (!std::isfinite(value) || value < std::numeric_limits<long long>::min() ||
                        value > std::numeric_limits<long long>::max())
                        return false;
                    result = VarValue(static_cast<long long>(value));
                    return true;
                }
                case Type_t::FLOAT: result = VarValue(source.getFloat()); return true;
                default: return false;
            }
        }
    }  // namespace

    bool ASTChecker::visit(Initializer& node)
    {
        // 示例实现：单个初始化器的语义检查
        // 1) 访问初始化值表达式
        // 2) 将子表达式的属性拷贝到当前节点
        if (!node.init_val)
        {
            reportError(node, "missing initializer expression");
            node.attr.val.value.type  = voidType;
            node.attr.val.isConstexpr = false;
            return false;
        }
        bool res  = apply(*this, *node.init_val);
        node.attr = node.init_val->attr;
        return res;
    }

    bool ASTChecker::visit(InitializerList& node)
    {
        // 示例实现：初始化器列表的语义检查
        // 遍历列表中的每个初始化器并逐个访问
        if (!node.init_list) return true;
        bool res = true;
        for (auto* init : *(node.init_list))
        {
            if (!init) continue;
            res &= apply(*this, *init);
        }
        return res;
    }

    bool ASTChecker::visit(VarDeclarator& node)
    {
        // TODO(Lab3-1): 实现变量声明器的语义检查
        // 访问左值表达式，同步属性，处理初始化器（如果有）
        auto* lval = dynamic_cast<LeftValExpr*>(node.lval);
        if (!lval || !lval->entry)
        {
            reportError(node, "invalid variable declarator");
            return false;
        }

        bool result     = true;
        bool registered = false;
        if (symTable.isGlobalScope() && funcDecls.count(lval->entry) != 0)
        {
            reportError(node, "global variable '" + lval->entry->getName() + "' conflicts with a function");
            result = false;
        }
        else if (isDeclaredInCurrentScope(lval->entry))
        {
            reportError(node, "redefinition of variable '" + lval->entry->getName() + "'");
            result = false;
        }
        else
        {
            VarAttr varAttr(currentDeclType ? currentDeclType : voidType, currentDeclIsConst, symTable.getScopeDepth());

            // 声明中的下标是维度表达式，而不是元素访问。
            // 在注册新变量前求值，避免新变量遮蔽维度中引用的外层常量。
            if (lval->indices)
            {
                for (auto* dim : *lval->indices)
                {
                    if (!dim)
                    {
                        reportError(node, "missing array dimension");
                        result = false;
                        continue;
                    }
                    if (!apply(*this, *dim))
                    {
                        result = false;
                        continue;
                    }
                    const auto& value = dim->attr.val;
                    Type* dimType = value.value.type;
                    if (!dimType || dimType->getTypeGroup() != TypeGroup::BASIC ||
                        (dimType->getBaseType() != Type_t::INT && dimType->getBaseType() != Type_t::LL) ||
                        !value.isConstexpr)
                    {
                        reportError(*dim, "array dimension must be an integer constant expression");
                        result = false;
                        continue;
                    }
                    const long long size = value.getLL();
                    if (size <= 0 || size > std::numeric_limits<int>::max())
                    {
                        reportError(*dim, "array dimension must be a positive integer within int range");
                        result = false;
                        continue;
                    }
                    varAttr.arrayDims.push_back(static_cast<int>(size));
                }
            }
            // 无效维度不进入符号表，避免保存不完整的形状。
            if (!result) return false;
            if (isTensor(varAttr.type) && varAttr.arrayDims.empty())
            {
                reportError(node, "tensor declaration requires at least one dimension");
                return false;
            }
            // 提前防止维度乘积或元素字节数溢出。
            size_t elementCount = 1;
            for (int dim : varAttr.arrayDims)
            {
                if (elementCount > std::numeric_limits<size_t>::max() / 4 / static_cast<size_t>(dim))
                {
                    reportError(node, "array size overflows addressable storage");
                    return false;
                }
                elementCount *= static_cast<size_t>(dim);
            }

            symTable.addSymbol(lval->entry, varAttr);
            rememberCurrentScope(lval->entry);
            registered = true;

            lval->isLval               = true;
            lval->attr.val.value.type  = varAttr.type;
            lval->attr.val.isConstexpr = false;
            lval->attr.arrayDims = varAttr.arrayDims;
        }

        const bool isScalarDeclarator = !lval->indices || lval->indices->empty();
        node.arrayInitEntries.clear();
        if (node.init)
        {
            const bool initOk = apply(*this, *node.init);
            result &= initOk;
            node.attr = node.init->attr;

            VarAttr* stored = symTable.getSymbol(lval->entry);
            if (registered && stored && !isScalarDeclarator && initOk)
            {
                // suffix[d] 是从第 d 维开始的子数组容量，最后一项为标量容量 1。
                const auto& dims = stored->arrayDims;
                const size_t rank = dims.size();
                std::vector<size_t> suffix(rank + 1, 1);
                for (size_t d = rank; d > 0; --d)
                    suffix[d - 1] = suffix[d] * static_cast<size_t>(dims[d - 1]);

                if (node.init->singleInit)
                {
                    // 普通数组仍只能用列表初始化；张量允许同类型同形状的表达式。
                    if (!isTensor(stored->type))
                    {
                        reportError(node, "array initializer must be a brace-enclosed list");
                        result = false;
                    }
                    else if (node.init->attr.val.value.type != stored->type ||
                              !tensorShapesCompatible(node.init->attr.arrayDims, dims))
                    {
                        reportError(node, "tensor initializer type or shape mismatch");
                        result = false;
                    }
                    else if (symTable.isGlobalScope() || stored->isConstDecl)
                    {
                        // 本阶段尚未实现整个张量的常量折叠；沿用全局/const 初值限制。
                        reportError(node, "global or const tensor expression initializer is not supported yet");
                        result = false;
                    }
                }
                else
                {
                    // 全局/const 数组需要完整的编译期值；普通局部数组保留表达式。
                    const bool requireConstant = symTable.isGlobalScope() || stored->isConstDecl;
                    Type* elementType = isTensor(stored->type)
                        ? TypeFactory::getBasicType(stored->type->getBaseType()) : stored->type;
                    const VarValue zero = elementType == floatType ? VarValue(0.0f) : VarValue(0);
                    std::vector<VarValue> values;
                    if (requireConstant)
                    {
                        if (suffix[0] > values.max_size())
                        {
                            reportError(node, "array initializer is too large");
                            return false;
                        }
                        try { values.assign(suffix[0], zero); }
                        catch (const std::bad_alloc&)
                        {
                            reportError(node, "array initializer is too large");
                            return false;
                        }
                    }

                    // 每次递归只允许填充当前子对象的范围。
                    // 遇到花括号时，选取当前位置对齐的最大下一级子对象；
                    // 未对齐时继续下降到较小子对象（必要时为标量）。
                    auto flatten = [&](auto&& self, InitDecl* init, size_t depth, size_t start) -> bool {
                        if (auto* scalar = dynamic_cast<Initializer*>(init))
                        {
                            if (!scalar->init_val || !isScalar(scalar->attr.val.value.type))
                            {
                                reportError(*init, "array initializer element must be scalar");
                                return false;
                            }
                            const auto& value = scalar->attr.val;
                            if (requireConstant && !value.isConstexpr)
                            {
                                reportError(*init, "array initializer requires a constant expression");
                                return false;
                            }
                            if (value.isConstexpr)
                            {
                                VarValue converted;
                                if (!convertConstant(value.value, elementType, converted))
                                {
                                    reportError(*init, "array constant initializer is out of range");
                                    return false;
                                }
                                if (requireConstant) values[start] = converted;
                            }
                            node.arrayInitEntries.emplace_back(start, scalar->init_val);
                            return true;
                        }
                        auto* list = dynamic_cast<InitializerList*>(init);
                        if (!list)
                        {
                            reportError(node, "invalid array initializer");
                            return false;
                        }
                        if (!list->init_list) return true;
                        size_t used = 0;
                        bool ok = true;
                        const size_t capacity = suffix[depth];
                        for (auto* child : *list->init_list)
                        {
                            if (!child)
                            {
                                reportError(*list, "missing array initializer element");
                                ok = false;
                                continue;
                            }
                            if (used >= capacity)
                            {
                                reportError(*child, "too many array initializer elements for subobject");
                                return false;
                            }
                            size_t childDepth = rank;
                            if (!child->singleInit && depth < rank)
                            {
                                childDepth = depth + 1;
                                while (childDepth < rank && used % suffix[childDepth] != 0)
                                    ++childDepth;
                            }
                            // 标量的额外嵌套花括号仍只占一个元素。
                            const size_t span = suffix[childDepth];
                            if (span > capacity - used)
                            {
                                reportError(*child, "too many array initializer elements for subobject");
                                return false;
                            }
                            ok &= self(self, child, childDepth, start + used);
                            used += span;
                        }
                        return ok;
                    };
                    const bool layoutOk = flatten(flatten, node.init, 0, 0);
                    result &= layoutOk;
                    if (layoutOk && requireConstant) stored->initList = std::move(values);
                    if (!layoutOk) node.arrayInitEntries.clear();
                }
            }
            if (registered && stored && isScalarDeclarator)
            {
                if (!node.init->singleInit)
                {
                    reportError(
                        node, "initializer list is not valid for scalar variable '" + lval->entry->getName() + "'");
                    result = false;
                }
                else if (initOk)
                {
                    Type* initType = node.init->attr.val.value.type;
                    if (!isScalar(stored->type) || !isScalar(initType))
                    {
                        reportError(node,
                            "cannot initialize variable '" + lval->entry->getName() + "' of type '" +
                                stored->type->toString() + "' with expression of type '" +
                                (initType ? initType->toString() : std::string("<null>")) + "'");
                        result = false;
                    }
                    else if (symTable.isGlobalScope() && !node.init->attr.val.isConstexpr)
                    {
                        reportError(node,
                            "initializer for global variable '" + lval->entry->getName() +
                                "' is not a constant expression");
                        result = false;
                    }
                    else if (stored->isConstDecl && !node.init->attr.val.isConstexpr)
                    {
                        reportError(node,
                            "initializer for const variable '" + lval->entry->getName() +
                                "' is not a constant expression");
                        result = false;
                    }
                    else if (node.init->attr.val.isConstexpr)
                    {
                        VarValue converted;
                        if (!convertConstant(node.init->attr.val.value, stored->type, converted))
                        {
                            reportError(node,
                                "constant initializer is out of range for variable '" + lval->entry->getName() + "'");
                            result = false;
                        }
                        else
                            stored->initList.push_back(converted);
                    }
                }
            }
        }
        else
        {
            node.attr = lval->attr;
            if (registered && currentDeclIsConst)
            {
                reportError(node, "const variable '" + lval->entry->getName() + "' requires an initializer");
                result = false;
            }
        }

        if (registered && symTable.isGlobalScope())
        {
            VarAttr* stored = symTable.getSymbol(lval->entry);
            if (stored) glbSymbols[lval->entry] = *stored;
        }
        return result;
    }

    bool ASTChecker::visit(ParamDeclarator& node)
    {
        // TODO(Lab3-1): 实现函数形参的语义检查
        // 检查形参重定义，处理数组形参的类型退化，将形参加入符号表
        if (!node.entry)
        {
            reportError(node, "invalid function parameter");
            return false;
        }
        if (isDeclaredInCurrentScope(node.entry))
        {
            reportError(node, "redefinition of parameter '" + node.entry->getName() + "'");
            return false;
        }

        VarAttr attr(node.type ? node.type : voidType, false, symTable.getScopeDepth());
        node.attr.arrayDims.clear();
        if (isTensor(attr.type) && (!node.dims || node.dims->empty()))
        {
            reportError(node, "tensor parameter requires an omitted first dimension []");
            return false;
        }
        if (node.dims && !node.dims->empty())
        {
            if (!isValidVariableType(attr.type))
            {
                reportError(node, "array parameter requires int or float element type");
                return false;
            }
            size_t stride = 1;
            for (size_t d = 0; d < node.dims->size(); ++d)
            {
                auto* dim = (*node.dims)[d];
                if (!dim || !apply(*this, *dim)) return false;
                const auto& value = dim->attr.val;
                Type* dimType = value.value.type;
                if (!dimType || dimType->getTypeGroup() != TypeGroup::BASIC ||
                    (dimType->getBaseType() != Type_t::INT && dimType->getBaseType() != Type_t::LL) ||
                    !value.isConstexpr)
                {
                    reportError(*dim, "array parameter dimension must be an integer constant expression");
                    return false;
                }
                const long long extent = value.getLL();
                // Lab2 用 LiteralExpr(-1) 表示首维省略的 []。
                auto* literal = dynamic_cast<LiteralExpr*>(dim);
                const bool omitted = d == 0 && literal && literal->literal.type == intType &&
                    literal->literal.getInt() == -1;
                if (isTensor(attr.type) && d == 0 && !omitted)
                {
                    reportError(*dim, "tensor parameter first dimension must be omitted []");
                    return false;
                }
                if (!omitted && (extent <= 0 || extent > std::numeric_limits<int>::max()))
                {
                    reportError(*dim, "array parameter dimension must be a positive integer");
                    return false;
                }
                if (d == 0)
                {
                    // 数组退化：首维不参与指针步长或实参形状匹配。
                    attr.arrayDims.push_back(-1);
                    continue;
                }
                const size_t n = static_cast<size_t>(extent);
                if (stride > std::numeric_limits<size_t>::max() / 4 / n)
                {
                    reportError(*dim, "array parameter stride overflows addressable storage");
                    return false;
                }
                stride *= n;
                attr.arrayDims.push_back(static_cast<int>(extent));
            }
        }
        symTable.addSymbol(node.entry, attr);
        rememberCurrentScope(node.entry);
        node.attr.arrayDims = attr.arrayDims;
        node.attr.val.value.type = isTensor(attr.type) || attr.arrayDims.empty()
            ? attr.type : TypeFactory::getPtrType(attr.type);
        node.attr.val.isConstexpr = false;
        return true;
    }

    bool ASTChecker::visit(VarDeclaration& node)
    {
        // TODO(Lab3-1): 实现变量声明的语义检查
        // 遍历声明列表，检查重定义，处理数组维度和初始化，将符号加入符号表
        if (!node.decls) return true;

        Type* oldType      = currentDeclType;
        bool  oldIsConst   = currentDeclIsConst;
        currentDeclType    = node.type;
        currentDeclIsConst = node.isConstDecl;

        bool result = true;
        if (!isValidVariableType(node.type))
        {
            reportError(node,
                "variable declaration has invalid type '" +
                    (node.type ? node.type->toString() : std::string("<null>")) + "'");
            result = false;
        }
        try
        {
            for (auto* decl : *node.decls)
                if (decl) result &= apply(*this, *decl);
        } catch (...)
        {
            currentDeclType    = oldType;
            currentDeclIsConst = oldIsConst;
            throw;
        }

        currentDeclType    = oldType;
        currentDeclIsConst = oldIsConst;
        return result;
    }
}  // namespace FE::AST
