#pragma once

#include <vector>

#include "ast_stmt_node.hpp"
#include "ast_stmts.hpp"
#include "ast/expr/ast_expr_node.hpp"


class TxElseClauseNode : public TxStatementNode {
public:
    TxStatementNode* body;

    TxElseClauseNode( const TxLocation& ploc, TxStatementNode* suite )
            : TxStatementNode( ploc ), body( suite ) {
    }

    virtual TxElseClauseNode* make_ast_copy() const override {
        return new TxElseClauseNode( this->ploc, this->body->make_ast_copy() );
    }

    virtual void symbol_resolution_pass() override {
        this->body->symbol_resolution_pass();
    }

    virtual bool may_end_with_non_return_stmt() const override {
        return this->body->may_end_with_non_return_stmt();
    }
    virtual bool ends_with_terminal_stmt() const override {
        return this->body->ends_with_terminal_stmt();
    }
    virtual bool ends_with_return_stmt() const override {
        return this->body->ends_with_return_stmt();
    }

    virtual void code_gen( LlvmGenerationContext& context, GenScope* scope ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->body->visit_ast( visitor, thisCursor, "body", context );
    }
};


class TxFlowHeaderNode : public TxNode {
public:
    TxFlowHeaderNode( const TxLocation& ploc ) : TxNode( ploc ) { }

    virtual TxFlowHeaderNode* make_ast_copy() const override = 0;

    /** Returns the sub-scope-defining statement of this loop header, or null if none. */
    virtual TxStatementNode* get_stmt_predecessor() const {
        return nullptr;
    }

    virtual void         code_gen_init    ( LlvmGenerationContext& context, GenScope* scope ) const = 0;
    virtual llvm::Value* code_gen_cond    ( LlvmGenerationContext& context, GenScope* scope ) const = 0;
    virtual void         code_gen_prestep ( LlvmGenerationContext& context, GenScope* scope ) const = 0;
    virtual void         code_gen_poststep( LlvmGenerationContext& context, GenScope* scope ) const = 0;
};


class TxCondClauseNode : public TxFlowHeaderNode {
    TxMaybeConversionNode* condExpr;

public:
    TxCondClauseNode( const TxLocation& ploc, TxExpressionNode* condExpr )
        : TxFlowHeaderNode( ploc ), condExpr( new TxMaybeConversionNode( condExpr ) )  { }

    virtual TxCondClauseNode* make_ast_copy() const override {
        return new TxCondClauseNode( this->ploc, this->condExpr->originalExpr->make_ast_copy() );
    }

    virtual void symbol_resolution_pass() override {
        this->condExpr->insert_conversion( this->registry().get_builtin_type( TXBT_BOOL ) );
        this->condExpr->symbol_resolution_pass();
    }

    void         code_gen_init( LlvmGenerationContext& context, GenScope* scope ) const { }
    llvm::Value* code_gen_cond( LlvmGenerationContext& context, GenScope* scope ) const;
    void         code_gen_prestep( LlvmGenerationContext& context, GenScope* scope ) const { }
    void         code_gen_poststep( LlvmGenerationContext& context, GenScope* scope ) const { }

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->condExpr->visit_ast( visitor, thisCursor, "cond", context );
    }
};


class TxIsClauseNode : public TxFlowHeaderNode {
    TxExpressionNode* origValueExpr;
    const std::string valueName;
    TxTypeExpressionNode* typeExpr;
    TxLocalFieldDefNode* valueField = nullptr;

protected:
    virtual void declaration_pass() override {
        auto declScope = this->context().scope();
        this->valueField->declare_field( declScope, TXD_NONE, TXS_STACK );
    }

public:
    TxIsClauseNode( const TxLocation& ploc, TxExpressionNode* valueExpr, const std::string& valueName, TxTypeExpressionNode* typeExpr );

    virtual TxIsClauseNode* make_ast_copy() const override {
        return new TxIsClauseNode( this->ploc, this->origValueExpr->make_ast_copy(), this->valueName, this->typeExpr->make_ast_copy() );
    }

    virtual void symbol_resolution_pass() override;

    void         code_gen_init( LlvmGenerationContext& context, GenScope* scope ) const { }
    llvm::Value* code_gen_cond( LlvmGenerationContext& context, GenScope* scope ) const;
    void         code_gen_prestep( LlvmGenerationContext& context, GenScope* scope ) const;
    void         code_gen_poststep( LlvmGenerationContext& context, GenScope* scope ) const { }

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->valueField->visit_ast( visitor, thisCursor, "value", context );
    }
};


class TxForHeaderNode : public TxFlowHeaderNode {
    TxStatementNode* initStmt;
    TxExprStmtNode*  nextCond;
    TxStatementNode* stepStmt;

public:
    TxForHeaderNode( const TxLocation& ploc, TxStatementNode* initStmt, TxExpressionNode* nextCond, TxStatementNode* stepStmt )
        : TxFlowHeaderNode( ploc ), initStmt( initStmt ),
          nextCond( new TxExprStmtNode( nextCond ) ), stepStmt( stepStmt ) {
        this->nextCond->predecessor = this->initStmt;
        this->stepStmt->predecessor = this->nextCond;
    }

    virtual TxForHeaderNode* make_ast_copy() const override {
        return new TxForHeaderNode( this->ploc, this->initStmt->make_ast_copy(), this->nextCond->expr->make_ast_copy(),
                                    this->stepStmt->make_ast_copy() );
    }

    virtual TxStatementNode* get_stmt_predecessor() const override {
        return this->initStmt;
    }

    virtual void symbol_resolution_pass() override {
        this->initStmt->symbol_resolution_pass();
        this->nextCond->symbol_resolution_pass();
        this->stepStmt->symbol_resolution_pass();
    }

    void         code_gen_init( LlvmGenerationContext& context, GenScope* scope ) const;
    llvm::Value* code_gen_cond( LlvmGenerationContext& context, GenScope* scope ) const;
    void         code_gen_prestep( LlvmGenerationContext& context, GenScope* scope ) const { }
    void         code_gen_poststep( LlvmGenerationContext& context, GenScope* scope ) const;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->initStmt->visit_ast( visitor, thisCursor, "init", context );
        this->nextCond->visit_ast( visitor, thisCursor, "cond", context );
        this->stepStmt->visit_ast( visitor, thisCursor, "step", context );
    }
};

class TxInClauseNode : public TxFlowHeaderNode {
    const std::string valueName;
    const std::string iterName;
    TxExpressionNode* origSeqExpr;
    TxLocalFieldDefNode*   iterField = nullptr;
    TxExpressionNode* nextCond = nullptr;
    TxLocalFieldDefNode*   valueField = nullptr;
    TxDeclarationFlags iterDeclFlags = TXD_NONE;

protected:
    virtual void declaration_pass() override {
        //auto declScope = this->context().scope()->create_code_block_scope( *this );
        auto declScope = this->context().scope();
        this->iterField->declare_field( declScope, this->iterDeclFlags, TXS_STACK );
        this->valueField->declare_field( declScope, TXD_NONE, TXS_STACK );
        // TODO: check: (to prevent init expr from referencing this field, it is processed in the 'outer' scope, not in the new block scope)
    }

public:
    TxInClauseNode( const TxLocation& ploc, const std::string& valueName, const std::string& iterName, TxExpressionNode* seqExpr );

    TxInClauseNode( const TxLocation& ploc, const std::string& valueName, TxExpressionNode* seqExpr )
            : TxInClauseNode( ploc, valueName, valueName + "$iter", seqExpr ) {
        this->iterDeclFlags = TXD_IMPLICIT;
    }

    virtual TxInClauseNode* make_ast_copy() const override {
        return new TxInClauseNode( this->ploc, this->valueName, this->origSeqExpr->make_ast_copy() );
    }

    virtual void symbol_resolution_pass() override {
        this->iterField->symbol_resolution_pass();
        this->nextCond->symbol_resolution_pass();
        this->valueField->symbol_resolution_pass();
    }

    void         code_gen_init( LlvmGenerationContext& context, GenScope* scope ) const;
    llvm::Value* code_gen_cond( LlvmGenerationContext& context, GenScope* scope ) const;
    void         code_gen_prestep( LlvmGenerationContext& context, GenScope* scope ) const;
    void         code_gen_poststep( LlvmGenerationContext& context, GenScope* scope ) const { }

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->iterField->visit_ast( visitor, thisCursor, "iterator", context );
        this->nextCond->visit_ast( visitor, thisCursor, "cond", context );
        this->valueField->visit_ast( visitor, thisCursor, "value", context );
    }
};


class TxIfStmtNode : public TxStatementNode {
    TxFlowHeaderNode* header;
    TxStatementNode* body;
    TxElseClauseNode* elseClause;

protected:
    virtual void stmt_declaration_pass() override {
        this->lexContext._scope = this->context().scope()->create_code_block_scope( *this, "if" );
    }

public:
    TxIfStmtNode( const TxLocation& ploc, TxFlowHeaderNode* header, TxStatementNode* body,
                  TxElseClauseNode* elseClause = nullptr )
            : TxStatementNode( ploc ), header( header ), body( body ), elseClause( elseClause ) {
    }

    virtual TxIfStmtNode* make_ast_copy() const override {
        return new TxIfStmtNode( this->ploc, this->header->make_ast_copy(), this->body->make_ast_copy(),
                                 ( this->elseClause ? this->elseClause->make_ast_copy() : nullptr ) );
    }

    virtual bool may_end_with_non_return_stmt() const override {
        return ( this->body->may_end_with_non_return_stmt() || ( this->elseClause && this->elseClause->may_end_with_non_return_stmt() ) );
    }
    virtual bool ends_with_terminal_stmt() const override {
        return ( this->body->ends_with_terminal_stmt() && this->elseClause && this->elseClause->ends_with_terminal_stmt() );
    }
    virtual bool ends_with_return_stmt() const override {
        return ( this->body->ends_with_return_stmt() && this->elseClause && this->elseClause->ends_with_return_stmt() );
    }

    virtual void code_gen( LlvmGenerationContext& context, GenScope* scope ) const override;

    virtual void symbol_resolution_pass() override {
        this->header->symbol_resolution_pass();
        this->body->symbol_resolution_pass();
        if ( this->elseClause )
            this->elseClause->symbol_resolution_pass();
    }

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->header->visit_ast( visitor, thisCursor, "header", context );
        this->body->visit_ast( visitor, thisCursor, "then", context );
        if (this->elseClause)
            this->elseClause->visit_ast( visitor, thisCursor, "else", context );
    }
};


class TxForStmtNode : public TxStatementNode {
    std::vector<TxFlowHeaderNode*>* loopHeaders;
    TxStatementNode* body;
    TxElseClauseNode* elseClause;

protected:
    virtual void stmt_declaration_pass() override {
        this->lexContext._scope = this->context().scope()->create_code_block_scope( *this, "lp" );
    }

public:
    TxForStmtNode( const TxLocation& ploc, std::vector<TxFlowHeaderNode*>* loopHeaders, TxStatementNode* body,
                   TxElseClauseNode* elseClause = nullptr )
            : TxStatementNode( ploc ), loopHeaders( loopHeaders ), body( body ), elseClause( elseClause )  {
        this->body->predecessor = this->loopHeaders->back()->get_stmt_predecessor();
        if ( this->elseClause )
            this->elseClause->predecessor = this->body->predecessor;
    }

    TxForStmtNode( const TxLocation& ploc, TxFlowHeaderNode* loopHeader, TxStatementNode* body, TxElseClauseNode* elseClause = nullptr )
            : TxForStmtNode( ploc, new std::vector<TxFlowHeaderNode*>( { loopHeader } ), body, elseClause )  { }

    virtual TxForStmtNode* make_ast_copy() const override {
        return new TxForStmtNode( this->ploc, make_node_vec_copy( this->loopHeaders ), body->make_ast_copy() );
    }

    virtual void symbol_resolution_pass() override {
        for ( auto header : *this->loopHeaders )
            header->symbol_resolution_pass();
        this->body->symbol_resolution_pass();
        if ( this->elseClause )
            this->elseClause->symbol_resolution_pass();
    }

    virtual void code_gen( LlvmGenerationContext& context, GenScope* scope ) const override;

    virtual bool may_end_with_non_return_stmt() const override {
        // FUTURE: handle break & continue that terminate statement outside this loop
        return false;
    }
    virtual bool ends_with_terminal_stmt() const override {
        // FUTURE: handle break & continue that terminate statement outside this loop
        //return ( this->body->ends_with_terminal_stmt() && this->elseClause && this->elseClause->ends_with_terminal_stmt() );
        return this->ends_with_return_stmt();
    }

    virtual bool ends_with_return_stmt() const override {
        return ( this->body->ends_with_return_stmt() && this->elseClause && this->elseClause->ends_with_return_stmt() );
    }

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        for ( auto header : *this->loopHeaders )
            header->visit_ast( visitor, thisCursor, "header", context );
        this->body->visit_ast( visitor, thisCursor, "body", context );
        if ( this->elseClause )
            this->elseClause->visit_ast( visitor, thisCursor, "else", context );
    }
};
