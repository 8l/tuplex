#pragma once

#include "ast_expr_node.hpp"
#include "ast_maybe_conv_node.hpp"
#include "ast_lit.hpp"

#include "tx_operations.hpp"


class TxOperatorValueNode : public TxExpressionNode {
public:
    TxOperatorValueNode( const TxLocation& ploc )
            : TxExpressionNode( ploc ) {
    }
};

/** A binary operation on two values of elementary types. */
class TxBinaryElemOperatorNode : public TxOperatorValueNode {
protected:
    virtual const TxQualType* define_type() override;

public:
    const TxOperation op;
    TxMaybeConversionNode* lhs;
    TxMaybeConversionNode* rhs;
    const int op_class;

    TxBinaryElemOperatorNode( const TxLocation& ploc, TxExpressionNode* lhs, const TxOperation op, TxExpressionNode* rhs )
            : TxOperatorValueNode( ploc ), op( op ),
              lhs( new TxMaybeConversionNode( lhs ) ), rhs( new TxMaybeConversionNode( rhs ) ), op_class( get_op_class( op ) ) {
        ASSERT( is_valid( op ), "Invalid operator value: " << (int)op );
    }

    virtual TxBinaryElemOperatorNode* make_ast_copy() const override {
        return new TxBinaryElemOperatorNode( this->ploc, this->lhs->originalExpr->make_ast_copy(), this->op,
                                         this->rhs->originalExpr->make_ast_copy() );
    }

    virtual void symbol_resolution_pass() override {
        TxExpressionNode::symbol_resolution_pass();
        lhs->symbol_resolution_pass();
        rhs->symbol_resolution_pass();
    }

    virtual bool is_statically_constant() const override {
        return this->lhs->is_statically_constant() && this->rhs->is_statically_constant();
    }

    virtual llvm::Constant* code_gen_const_value( LlvmGenerationContext& context ) const override;
    virtual llvm::Value* code_gen_dyn_value( LlvmGenerationContext& context, GenScope* scope ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->lhs->visit_ast( visitor, thisCursor, "lhs", context );
        this->rhs->visit_ast( visitor, thisCursor, "rhs", context );
    }

    virtual const std::string& get_descriptor() const override {
        return to_string( this->op );
    }
};

class TxUnaryMinusNode : public TxOperatorValueNode {
protected:
    virtual const TxQualType* define_type() override;

public:
    TxMaybeConversionNode* operand;
    TxUnaryMinusNode( const TxLocation& ploc, TxExpressionNode* operand )
            : TxOperatorValueNode( ploc ), operand( new TxMaybeConversionNode( operand ) ) {
        if ( auto intLit = dynamic_cast<TxIntegerLitNode*>( this->operand->originalExpr ) ) {
            intLit->set_negative();
        }
    }

    virtual TxUnaryMinusNode* make_ast_copy() const override {
        return new TxUnaryMinusNode( this->ploc, this->operand->originalExpr->make_ast_copy() );
    }

    virtual void symbol_resolution_pass() override {
        TxExpressionNode::symbol_resolution_pass();
        operand->symbol_resolution_pass();
    }

    virtual bool is_statically_constant() const override {
        return this->operand->is_statically_constant();
    }

    virtual llvm::Constant* code_gen_const_value( LlvmGenerationContext& context ) const override;
    virtual llvm::Value* code_gen_dyn_value( LlvmGenerationContext& context, GenScope* scope ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->operand->visit_ast( visitor, thisCursor, "operand", context );
    }
};

class TxUnaryLogicalNotNode : public TxOperatorValueNode {
protected:
    virtual const TxQualType* define_type() override;

public:
    TxExpressionNode* operand;
    TxUnaryLogicalNotNode( const TxLocation& ploc, TxExpressionNode* operand )
            : TxOperatorValueNode( ploc ), operand( operand ) {
    }

    virtual TxUnaryLogicalNotNode* make_ast_copy() const override {
        return new TxUnaryLogicalNotNode( this->ploc, this->operand->make_ast_copy() );
    }

    virtual void symbol_resolution_pass() override {
        TxExpressionNode::symbol_resolution_pass();
        operand->symbol_resolution_pass();
        // assume arithmetic, scalar negation:
        if ( !operand->qualtype()->type()->is_builtin( TXBT_BOOL ) )
            // should we support any auto-conversion to Bool?
            CERROR( this, "Operand of unary '!' is not of Bool type: " << operand->qualtype() );
    }

    virtual bool is_statically_constant() const override {
        return this->operand->is_statically_constant();
    }

    virtual llvm::Constant* code_gen_const_value( LlvmGenerationContext& context ) const override;
    virtual llvm::Value* code_gen_dyn_value( LlvmGenerationContext& context, GenScope* scope ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->operand->visit_ast( visitor, thisCursor, "operand", context );
    }
};


/** Equality comparison between two values.
 *
 * Semantically, == and equals() are regarded as the same (value comparing) operation.
 *
 * == redirects to equals() for values that aren't elementary, reference, or function types.
 *
 * For elementary values, == is a built-in, bitwise comparison.
 * For function values, == is a built-in, bitwise comparison of function pointer and closure identity.
 * For reference values, == is a built-in, bitwise comparison, i.e. shallow compare.
 *
 * Array.equals() applies == on each element (so array of references will do shallow compare)
 *   Note: Even if Array.equals() had been defined as deep-compare, the question would have been how deep.
 *   Note: If having called equals() instead, and the references were to tuples, the default equals() is also shallow.
 *         If deep-compare of arrays of references to tuples is desired, the tuples must also override equals().
 *
 * Tuple.equals() defaults to Any.equals() which compares object identity.
 *
 * To do deep compare of references, either deference them (provided their type is known):
 * ref1^ == ref2^
 * or invoke the equals() method:
 * ref1.equals( ref2 )
 */
class TxEqualityOperatorNode : public TxOperatorValueNode {
protected:
    virtual const TxQualType* define_type() override;

public:
    TxMaybeConversionNode* lhs;
    TxMaybeConversionNode* rhs;

    TxEqualityOperatorNode( const TxLocation& ploc, TxExpressionNode* lhs, TxExpressionNode* rhs )
            : TxOperatorValueNode( ploc ), lhs( new TxMaybeConversionNode( lhs ) ), rhs( new TxMaybeConversionNode( rhs ) ) {
    }

    virtual TxEqualityOperatorNode* make_ast_copy() const override {
        return new TxEqualityOperatorNode( this->ploc, this->lhs->originalExpr->make_ast_copy(), this->rhs->originalExpr->make_ast_copy() );
    }

    virtual void symbol_resolution_pass() override;

    virtual bool is_statically_constant() const override;

    virtual llvm::Constant* code_gen_const_value( LlvmGenerationContext& context ) const override;
    virtual llvm::Value* code_gen_dyn_value( LlvmGenerationContext& context, GenScope* scope ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->lhs->visit_ast( visitor, thisCursor, "lhs", context );
        this->rhs->visit_ast( visitor, thisCursor, "rhs", context );
    }
};

/** Equality comparison whether two Ref values refer to the same instance (identity equality). */
class TxRefEqualityOperatorNode : public TxOperatorValueNode {
protected:
    virtual const TxQualType* define_type() override;

public:
    TxMaybeConversionNode* lhs;
    TxMaybeConversionNode* rhs;

    TxRefEqualityOperatorNode( const TxLocation& ploc, TxExpressionNode* lhs, TxExpressionNode* rhs )
            : TxOperatorValueNode( ploc ), lhs( new TxMaybeConversionNode( lhs ) ), rhs( new TxMaybeConversionNode( rhs ) ) {
    }

    virtual TxRefEqualityOperatorNode* make_ast_copy() const override {
        return new TxRefEqualityOperatorNode( this->ploc, this->lhs->originalExpr->make_ast_copy(), this->rhs->originalExpr->make_ast_copy() );
    }

    virtual void symbol_resolution_pass() override {
        TxExpressionNode::symbol_resolution_pass();
        lhs->symbol_resolution_pass();
        rhs->symbol_resolution_pass();
    }

    virtual bool is_statically_constant() const override {
        return this->lhs->is_statically_constant() && this->rhs->is_statically_constant();
    }

    virtual llvm::Constant* code_gen_const_value( LlvmGenerationContext& context ) const override;
    virtual llvm::Value* code_gen_dyn_value( LlvmGenerationContext& context, GenScope* scope ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->lhs->visit_ast( visitor, thisCursor, "lhs", context );
        this->rhs->visit_ast( visitor, thisCursor, "rhs", context );
    }
};
