#pragma once

#include "../symbol/qual_type.hpp"
#include "ast/ast_node.hpp"
#include "ast/ast_fielddef_node.hpp"
#include "ast/ast_util.hpp"

#include "symbol/type_registry.hpp"

/** Common superclass for non-local entity declaration nodes. */
class TxDeclarationNode : public TxNode {
    TxDeclarationFlags declFlags;
    // these set TXD_EXPERRBLOCK if this is an exp-err declaration:
    friend class TxExpErrDeclNode;
    friend class TxTypeStmtNode;  // forwards from TxExpErrStmtNode

public:
    TxDeclarationNode( const TxLocation& ploc, const TxDeclarationFlags declFlags )
            : TxNode( ploc ), declFlags( declFlags ) {
        if ( !( declFlags & TXD_PROTECTED ) )
            this->declFlags |= TXD_PUBLIC;  // public is currently the default
    }

    virtual TxDeclarationNode* make_ast_copy() const override = 0;

    inline TxDeclarationFlags get_decl_flags() const {
        return this->declFlags;
    }

    virtual const TxEntityDeclaration* get_declaration() const = 0;

    virtual void code_gen( LlvmGenerationContext& context ) const = 0;
};

/** Non-local field declaration */
class TxFieldDeclNode : public TxDeclarationNode {
    const bool isMethodSyntax = false;

protected:
    virtual void declaration_pass() override;

public:
    TxNonLocalFieldDefNode* fieldDef;

    TxFieldDeclNode( const TxLocation& ploc, const TxDeclarationFlags declFlags, TxNonLocalFieldDefNode* field,
                     bool isMethodSyntax = false )
            : TxDeclarationNode( ploc, declFlags ), isMethodSyntax( isMethodSyntax ), fieldDef( field ) {
    }

    virtual TxFieldDeclNode* make_ast_copy() const override {
        return new TxFieldDeclNode( this->ploc, this->get_decl_flags(), this->fieldDef->make_ast_copy(), this->isMethodSyntax );
    }

    virtual void symbol_resolution_pass() override;

    virtual const TxFieldDeclaration* get_declaration() const override {
        return this->fieldDef->get_declaration();
    }

    virtual void code_gen( LlvmGenerationContext& context ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->fieldDef->visit_ast( visitor, thisCursor, "field", context );
    }

    virtual const std::string& get_descriptor() const override {
        return this->fieldDef->get_descriptor();
    }
};

/** Both non-local and local type declarations */
class TxTypeDeclNode : public TxDeclarationNode {
    /** if true, this node's subtree is merged with a built-in type definition */
    bool _builtinCode = false;

protected:
    virtual void declaration_pass() override;

public:
    const TxIdentifier* typeName;
    const bool interfaceKW;
    const bool mutableType;
    const std::vector<TxDeclarationNode*>* typeParamDecls;
    TxTypeExpressionNode* typeExpression;

    TxTypeDeclNode( const TxLocation& ploc, const TxDeclarationFlags declFlags, const std::string& typeName,
                    const std::vector<TxDeclarationNode*>* typeParamDecls, TxTypeExpressionNode* typeExpression,
                    bool interfaceKW = false, bool mutableType = false )
            : TxDeclarationNode( ploc, declFlags ), typeName( new TxIdentifier( typeName ) ),
              interfaceKW( interfaceKW ), mutableType( mutableType ), typeParamDecls( typeParamDecls ), typeExpression( typeExpression ) {
        typeExpression->set_interface( interfaceKW );
        typeExpression->set_requires_mutable( mutableType );
    }

    virtual TxTypeDeclNode* make_ast_copy() const override {
        return new TxTypeDeclNode( this->ploc, this->get_decl_flags(), this->typeName->str(),
                                   make_node_vec_copy( this->typeParamDecls ), this->typeExpression->make_ast_copy(),
                                   this->interfaceKW, this->mutableType );
    }

    virtual void symbol_resolution_pass() override;

    virtual const TxTypeDeclaration* get_declaration() const override {
        return this->typeExpression->get_declaration();
    }

    virtual void code_gen( LlvmGenerationContext& context ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        if ( !this->_builtinCode && this->typeParamDecls )
            for ( auto decl : *this->typeParamDecls )
                decl->visit_ast( visitor, thisCursor, "type-param", context );
        this->typeExpression->visit_ast( visitor, thisCursor, "type", context );
    }

    virtual const std::string& get_descriptor() const override {
        return this->typeName->str();
    }
};

class TxExpErrDeclNode : public TxDeclarationNode {
    ExpectedErrorClause* expError;

protected:
    virtual void declaration_pass() override;

public:
    TxDeclarationNode* body;

    TxExpErrDeclNode( const TxLocation& ploc, ExpectedErrorClause* expError, TxDeclarationNode* body )
            : TxDeclarationNode( ploc, ( body ? body->get_decl_flags() : TXD_NONE ) | TXD_EXPERRBLOCK ),
              expError( expError ), body( body ) {
        if ( body ) {
            body->declFlags |= TXD_EXPERRBLOCK;
            if ( dynamic_cast<const TxExpErrDeclNode*>( body ) )
                CERROR( this, "Can't nest Expected Error constructs in a declaration" );
        }
    }

    virtual TxExpErrDeclNode* make_ast_copy() const override {
        return new TxExpErrDeclNode( this->ploc, nullptr, ( this->body ? this->body->make_ast_copy() : nullptr ) );
    }

    virtual void symbol_resolution_pass() override {
        if ( this->body ) {
            ScopedExpErrClause scopedEEClause( this );
            this->body->symbol_resolution_pass();
        }
    }

    virtual const TxEntityDeclaration* get_declaration() const override {
        return ( this->body ? this->body->get_declaration() : nullptr );
    }

    virtual void code_gen( LlvmGenerationContext& context ) const override { }

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        if ( this->body ) {
            ScopedExpErrClause scopedEEClause( this );
            this->body->visit_ast( visitor, thisCursor, "decl", context );
        }
    }
};
