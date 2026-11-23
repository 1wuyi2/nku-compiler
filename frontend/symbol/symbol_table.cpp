#include <frontend/symbol/symbol_table.h>
#include <debug.h>

namespace FE::Sym
{
    void SymTable::reset_impl()
    {
        scopes.clear();
        scopes.emplace_back();
    }

    void SymTable::enterScope_impl() { scopes.emplace_back(); }

    void SymTable::exitScope_impl()
    {
        // Keep the global scope alive when an unmatched exit is requested.
        if (scopes.size() > 1) scopes.pop_back();
    }

    void SymTable::addSymbol_impl(Entry* entry, FE::AST::VarAttr& attr)
    {
        if (entry == nullptr) return;

        // emplace deliberately preserves the first declaration in this scope.
        scopes.back().emplace(entry, attr);
    }

    FE::AST::VarAttr* SymTable::getSymbol_impl(Entry* entry)
    {
        if (entry == nullptr) return nullptr;

        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
        {
            auto symbol = scope->find(entry);
            if (symbol != scope->end()) return &symbol->second;
        }
        return nullptr;
    }

    bool SymTable::isGlobalScope_impl() { return scopes.size() == 1; }

    int SymTable::getScopeDepth_impl() { return static_cast<int>(scopes.size()) - 1; }
}  // namespace FE::Sym
