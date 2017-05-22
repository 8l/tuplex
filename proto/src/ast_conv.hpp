#pragma once

#include "ast_declbase.hpp"

/** Wraps the provided original expression with a new conversion expression node if necessary and permitted.
 * Assumes declaration pass has already run on originalExpr.
 * If a conversion node is created, symbol declaration pass is run on it.
 * If the types don't match and conversion is not possible, a compilation error is generated and null is returned.
 * @param _explicit if true, forces conversion between types that don't permit implicit conversion
 * @return a new conversion node that wraps the original node, or the original node itself if no conversion was applied, or null upon failure
 */
TxExpressionNode* make_conversion( TxExpressionNode* originalExpr, const TxType* resultType, bool _explicit );

/** A specific conversion of an expression to a resulting type. */
class TxConversionNode : public TxExpressionNode {
protected:
    virtual const TxType* define_type() override {
        return this->resultType;
    }
public:
    TxExpressionNode* expr;
    TxType const * const resultType;

    TxConversionNode( TxExpressionNode* expr, const TxType* resultType )
            : TxExpressionNode( expr->parseLocation ), expr( expr ), resultType( resultType ) {
        ASSERT( resultType, "NULL resultType" );
    }

    virtual TxConversionNode* make_ast_copy() const override {
        ASSERT( false, "Can't make AST copy of a TxConversionNode: " << this );
        return nullptr;
    }

    virtual void symbol_resolution_pass() override {
        TxExpressionNode::symbol_resolution_pass();
        this->expr->symbol_resolution_pass();
    }

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->expr->visit_ast( visitor, thisCursor, "convertee", context );
    }
};

class TxScalarConvNode : public TxConversionNode {
public:
    TxScalarConvNode( TxExpressionNode* expr, const TxType* scalarResultType )
            : TxConversionNode( expr, scalarResultType ) {
    }

    virtual bool is_statically_constant() const override {
        return this->expr->is_statically_constant();
    }

    virtual llvm::Constant* code_gen_constant( llvm::LLVMContext& llvmContext ) const override;
    virtual llvm::Value* code_gen( LlvmGenerationContext& context, GenScope* scope ) const override;
};

class TxBoolConvNode : public TxConversionNode {
public:
    TxBoolConvNode( TxExpressionNode* expr, const TxType* boolResultType )
            : TxConversionNode( expr, boolResultType ) {
    }

    virtual bool is_statically_constant() const override {
        return this->expr->is_statically_constant();
    }

    virtual llvm::Constant* code_gen_constant( llvm::LLVMContext& llvmContext ) const override;
    virtual llvm::Value* code_gen( LlvmGenerationContext& context, GenScope* scope ) const override;
};

class TxReferenceConvNode : public TxConversionNode {
    const TxType* adapterType = nullptr;
    protected:
    virtual const TxType* define_type() override;

public:
    TxReferenceConvNode( TxExpressionNode* expr, const TxType* refResultType )
            : TxConversionNode( expr, refResultType ) {
    }
    virtual llvm::Value* code_gen( LlvmGenerationContext& context, GenScope* scope ) const override;
};

/** Casts (not converts) between object specializations (across type parameters and inheritance). */
class TxObjSpecCastNode : public TxConversionNode {
public:
    TxObjSpecCastNode( TxExpressionNode* expr, const TxType* resultType )
            : TxConversionNode( expr, resultType ) {
    }
    virtual llvm::Value* code_gen( LlvmGenerationContext& context, GenScope* scope ) const override;
};

/** A non-conversion "placeholder conversion". */
class TxNoConversionNode : public TxConversionNode {
public:
    TxNoConversionNode( TxExpressionNode* expr, const TxType* resultType )
            : TxConversionNode( expr, resultType ) {
    }
    virtual llvm::Value* code_gen( LlvmGenerationContext& context, GenScope* scope ) const override {
        return this->expr->code_gen( context, scope );
    }
};
