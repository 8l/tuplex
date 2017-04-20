#pragma once

#include "ast_declbase.hpp"
#include "ast_types.hpp"



/** Represents explicit array literals in source code as well as array initializers created implicitly (e.g. for var-arg functions).
 * Note that an array literal doesn't necessarily only have literal elements; it is statically constant only if all its elements are.
 */
class TxArrayLitNode : public TxExpressionNode {
    std::vector<TxExpressionNode*> const * const origElemExprList;
    TxTypeTypeArgumentNode* elementTypeNode;
    TxValueTypeArgumentNode* lengthNode;
    bool _constant;

protected:
    virtual const TxType* define_type() override {
        auto elemType = this->elementTypeNode->typeExprNode->resolve_type();
        for ( auto elemExprI = this->elemExprList->begin() + 1; elemExprI != this->elemExprList->end(); elemExprI++ )
            (*elemExprI)->insert_conversion( elemType );
        return this->registry().get_array_type( this, this->elementTypeNode, this->lengthNode );
    }

public:
    std::vector<TxMaybeConversionNode*> const * const elemExprList;

    TxArrayLitNode(const TxLocation& parseLocation, const std::vector<TxExpressionNode*>* elemExprList);

    /** Creates an array literal node with elements that are owned by another AST node.
     * The resulting array literal node may not be AST-copied. */
    TxArrayLitNode(const TxLocation& parseLocation, const std::vector<TxMaybeConversionNode*>* elemExprList);

    virtual TxArrayLitNode* make_ast_copy() const override {
        if (! this->origElemExprList) {
            ASSERT(false, "Can't make AST copy of a TxArrayLitNode whose elements are owned by another AST node: " << this);
            return nullptr;
        }
        return new TxArrayLitNode( this->parseLocation, make_node_vec_copy( this->origElemExprList ) );
    }

    virtual void symbol_declaration_pass( LexicalContext& lexContext) override {
        this->set_context( lexContext);
        this->elementTypeNode->symbol_declaration_pass( lexContext, lexContext );
        this->lengthNode->symbol_declaration_pass( lexContext, lexContext );
        if (this->origElemExprList) {
            // if this node owns the element nodes, perform declaration pass on them:
            for (auto elemExpr : *this->elemExprList)
                elemExpr->symbol_declaration_pass( lexContext);
        }
    }

    virtual void symbol_resolution_pass() override {
        TxExpressionNode::symbol_resolution_pass();
        this->elementTypeNode->symbol_resolution_pass();
        this->lengthNode->symbol_resolution_pass();
        for (auto elemExpr : *this->elemExprList)
            elemExpr->symbol_resolution_pass();

        this->_constant = true;
        for (auto arg : *this->elemExprList) {
            if (!arg->is_statically_constant()) {
                this->_constant = false;
                break;
            }
        }
    }

    virtual bool is_stack_allocation_expression() const override { return true; }

    virtual bool is_statically_constant() const override { return this->_constant; }

    virtual llvm::Value* code_gen_address(LlvmGenerationContext& context, GenScope* scope) const override;
    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstParent& thisAsParent, const std::string& role, void* context ) const override {
        for (auto elem : *this->elemExprList)
            elem->visit_ast( visitor, thisAsParent, "element", context );
    }
};
