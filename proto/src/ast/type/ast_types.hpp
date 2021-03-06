#pragma once

#include "ast/ast_entitydecls.hpp"
#include "ast/ast_entitydefs.hpp"
#include "ast/type/ast_typeexpr_node.hpp"
#include "ast/type/ast_typearg_node.hpp"
#include "ast/type/ast_argtypedef_node.hpp"
#include "ast/expr/ast_maybe_conv_node.hpp"

#include "symbol/type_registry.hpp"
#include "symbol/qual_type.hpp"

class TxIdentifiedSymbolNode : public TxTypeDefiningNode {
    const TxIdentifier* symbolName;
    TxScopeSymbol* symbol = nullptr;

    friend class TxNamedTypeNode;

protected:
    TxScopeSymbol* resolve_symbol();

    virtual const TxQualType* define_type() override;

public:
    TxIdentifiedSymbolNode* baseSymbol;

    TxIdentifiedSymbolNode( const TxLocation& ploc, TxIdentifiedSymbolNode* baseSymbol, const std::string& name )
            : TxTypeDefiningNode( ploc ), symbolName( new TxIdentifier( name ) ), baseSymbol( baseSymbol ) {
    }

    virtual TxIdentifiedSymbolNode* make_ast_copy() const override {
        return new TxIdentifiedSymbolNode( this->ploc, ( this->baseSymbol ? this->baseSymbol->make_ast_copy() : nullptr ),
                                           this->symbolName->str() );
    }

    virtual void symbol_resolution_pass() {
        if ( this->baseSymbol )
            this->baseSymbol->symbol_resolution_pass();
    }

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        if ( this->baseSymbol )
            this->baseSymbol->visit_ast( visitor, thisCursor, "basetype", context );
    }

    /** Returns the full identifier (dot-separated full name) as specified in the program text, up to and including this name. */
    inline TxIdentifier get_full_identifier() const {
        return ( this->baseSymbol ? TxIdentifier( this->baseSymbol->get_full_identifier(), this->symbolName->str() ) : *this->symbolName );
    }

    virtual const std::string& get_descriptor() const override {
        return this->symbolName->str();
    }
};

/** Identifies a type directly via its name. */
class TxNamedTypeNode : public TxTypeExpressionNode {
protected:
    virtual const TxQualType* define_type() override;

public:
    TxIdentifiedSymbolNode* symbolNode;

    TxNamedTypeNode( const TxLocation& ploc, TxIdentifiedSymbolNode* symbolNode )
            : TxTypeExpressionNode( ploc ), symbolNode( symbolNode ) {
    }

    TxNamedTypeNode( const TxLocation& ploc, const std::string& name )
            : TxTypeExpressionNode( ploc ), symbolNode( new TxIdentifiedSymbolNode( ploc, nullptr, name ) ) {
    }

    virtual TxNamedTypeNode* make_ast_copy() const override {
        return new TxNamedTypeNode( this->ploc, symbolNode->make_ast_copy() );
    }

    virtual void symbol_resolution_pass() override {
        TxTypeExpressionNode::symbol_resolution_pass();
        this->symbolNode->symbol_resolution_pass();
    }

    virtual void code_gen_type( LlvmGenerationContext& context ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->symbolNode->visit_ast( visitor, thisCursor, "symbol", context );
    }

    virtual const std::string& get_descriptor() const override {
        return this->symbolNode->get_descriptor();
    }
};

/** Identifies a type that is a member of another type, which is determined by an arbitrary type expression. */
class TxMemberTypeNode : public TxTypeExpressionNode {
protected:
    virtual const TxQualType* define_type() override;

public:
    TxTypeExpressionNode* baseTypeExpr;
    const std::string memberName;

    TxMemberTypeNode( const TxLocation& ploc, TxTypeExpressionNode* baseTypeExpr, const std::string& memberName )
            : TxTypeExpressionNode( ploc ), baseTypeExpr( baseTypeExpr ), memberName( memberName ) {
    }

    virtual TxMemberTypeNode* make_ast_copy() const override {
        return new TxMemberTypeNode( this->ploc, baseTypeExpr->make_ast_copy(), memberName );
    }

    virtual void symbol_resolution_pass() override {
        TxTypeExpressionNode::symbol_resolution_pass();
        this->baseTypeExpr->symbol_resolution_pass();
    }

    virtual void code_gen_type( LlvmGenerationContext& context ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->baseTypeExpr->visit_ast( visitor, thisCursor, "type-expr", context );
        //this->memberName->visit_ast( visitor, thisCursor, "member", context );
    }

    virtual const std::string& get_descriptor() const override {
        return this->memberName;
    }
};


/** Represents a specialization of a generic type (binding one or more type parameters).
 */
class TxGenSpecTypeNode : public TxTypeExpressionNode {
protected:
    virtual const TxQualType* define_type() override;

public:
    TxTypeExpressionNode* genTypeExpr;
    const std::vector<TxTypeArgumentNode*>* const typeArgs;

    TxGenSpecTypeNode( const TxLocation& ploc, TxTypeExpressionNode* genTypeExpr, const std::vector<TxTypeArgumentNode*>* typeArgs )
            : TxTypeExpressionNode( ploc ), genTypeExpr( genTypeExpr ), typeArgs( typeArgs ) {
        ASSERT( typeArgs && !typeArgs->empty(), "NULL or empty typeargs" );
    }

    virtual TxGenSpecTypeNode* make_ast_copy() const override {
        return new TxGenSpecTypeNode( this->ploc, this->genTypeExpr->make_ast_copy(), make_node_vec_copy( this->typeArgs ) );
    }

    virtual void symbol_resolution_pass() override {
        TxTypeExpressionNode::symbol_resolution_pass();
        this->genTypeExpr->symbol_resolution_pass();
        for ( TxTypeArgumentNode* ta : *this->typeArgs ) {
            ta->symbol_resolution_pass();

            if (this->genTypeExpr->qualtype()->get_type_class() != TXTC_REFERENCE) {
                if ( auto typeTypeArg = dynamic_cast<TxTypeTypeArgumentNode*>( ta ) ) {
                    auto elemType = typeTypeArg->typeExprNode->qualtype();
                    if ( is_not_properly_concrete( this, elemType->type() ) ) {
                        CERROR( this, "Type specialization parameter is not concrete: " << elemType );
                    }
                }
            }
        }
    }

    virtual void code_gen_type( LlvmGenerationContext& context ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->genTypeExpr->visit_ast( visitor, thisCursor, "gentype", context );
        for ( auto typeArg : *this->typeArgs )
            typeArg->visit_ast( visitor, thisCursor, "typearg", context );
    }

    virtual const std::string& get_descriptor() const override {
        return this->genTypeExpr->get_descriptor();
    }
};

/** Common superclass for specializations of the built-in types Ref and Array. */
class TxBuiltinTypeSpecNode : public TxTypeExpressionNode {
public:
    TxBuiltinTypeSpecNode( const TxLocation& ploc )
            : TxTypeExpressionNode( ploc ) {
    }
};

/**
 * Custom AST node needed to handle dataspaces. */
class TxReferenceTypeNode : public TxBuiltinTypeSpecNode {
    TxReferenceTypeNode( const TxLocation& ploc, const TxIdentifier* dataspace, TxTypeTypeArgumentNode* targetTypeArg )
            : TxBuiltinTypeSpecNode( ploc ), dataspace( dataspace ), targetTypeNode( targetTypeArg ) {
    }

protected:
    virtual const TxQualType* define_type() override {
        return new TxQualType( this->registry().get_reference_type( this, targetTypeNode, this->dataspace ) );
    }

public:
    const TxIdentifier* dataspace;
    TxTypeTypeArgumentNode* targetTypeNode;

    TxReferenceTypeNode( const TxLocation& ploc, const TxIdentifier* dataspace, TxTypeExpressionNode* targetType )
            : TxReferenceTypeNode( ploc, dataspace, new TxTypeTypeArgumentNode( targetType ) ) {
    }

    virtual TxReferenceTypeNode* make_ast_copy() const override {
        return new TxReferenceTypeNode( this->ploc, this->dataspace, this->targetTypeNode->make_ast_copy() );
    }

    virtual void symbol_resolution_pass() override {
        TxBuiltinTypeSpecNode::symbol_resolution_pass();
        this->targetTypeNode->symbol_resolution_pass();
    }

    virtual void code_gen_type( LlvmGenerationContext& context ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->targetTypeNode->visit_ast( visitor, thisCursor, "target", context );
    }
};

/**
 * Custom AST node needed to provide syntactic sugar for modifiable declaration. */
class TxArrayTypeNode : public TxBuiltinTypeSpecNode {
    TxArrayTypeNode( const TxLocation& ploc, TxTypeTypeArgumentNode* elementTypeArg, TxValueTypeArgumentNode* capacityExprArg )
            : TxBuiltinTypeSpecNode( ploc ), elementTypeNode( elementTypeArg ), capacityNode( capacityExprArg ) {
    }

protected:
    virtual const TxQualType* define_type() override;

public:
    TxTypeTypeArgumentNode* elementTypeNode;
    TxValueTypeArgumentNode* capacityNode;

    TxArrayTypeNode( const TxLocation& ploc, TxTypeExpressionNode* elementType, TxExpressionNode* capacityExpr = nullptr )
            : TxArrayTypeNode( ploc, new TxTypeTypeArgumentNode( elementType ),
                               ( capacityExpr ? new TxValueTypeArgumentNode( new TxMaybeConversionNode( capacityExpr ) ) : nullptr ) ) {
    }

    virtual TxArrayTypeNode* make_ast_copy() const override {
        return new TxArrayTypeNode( this->ploc, this->elementTypeNode->make_ast_copy(),
                                    ( this->capacityNode ? this->capacityNode->make_ast_copy() : nullptr ) );
    }

    virtual void symbol_resolution_pass() override {
        TxBuiltinTypeSpecNode::symbol_resolution_pass();
        this->elementTypeNode->symbol_resolution_pass();
        if ( this->capacityNode ) {
            this->capacityNode->symbol_resolution_pass();
        }
        auto elemType = this->elementTypeNode->typeExprNode->qualtype();
        if ( is_not_properly_concrete( this, elemType->type() ) ) {
            CERROR( this, "Array element type is not concrete: " << elemType );
        }
    }

    virtual void code_gen_type( LlvmGenerationContext& context ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->elementTypeNode->visit_ast( visitor, thisCursor, "elementtype", context );
        if ( this->capacityNode )
            this->capacityNode->visit_ast( visitor, thisCursor, "capacity", context );
    }
};

/** Represents a type derivation with a body and / or implemented interfaces (a.k.a. a type extension). */
class TxDerivedTypeNode : public TxTypeExpressionNode {
    TxTypeDeclNode* superRefTypeNode = nullptr;

    TxTypeDefiningNode* builtinTypeDefiner = nullptr;

    /** Initializes certain implicit type members such as 'Super' for types with a body. */
    void init_implicit_types();

    friend class TxBuiltinTypeDefiningNode;

    void set_builtin_type_definer( TxTypeDefiningNode* builtinTypeDefiner ) {
        this->builtinTypeDefiner = builtinTypeDefiner;
    }

    void code_gen_builtin_type( LlvmGenerationContext& context ) const;

    void inner_code_gen_type( LlvmGenerationContext& context ) const;

    /** used by make_ast_copy() */
    TxDerivedTypeNode( const TxLocation& ploc, TxTypeExpressionNode* baseType,
                       std::vector<TxTypeExpressionNode*>* interfaces, std::vector<TxDeclarationNode*>* members,
                       bool interfaceKW, bool mutableType )
            : TxTypeExpressionNode( ploc ), baseType( baseType ), interfaces( interfaces ), members( members ) {
    }

protected:
    virtual void typeexpr_declaration_pass() override {
        this->init_implicit_types();  // (can't run this before interfaceKW is known)
    }

    virtual const TxQualType* define_type() override;

public:
    TxTypeExpressionNode* baseType;
    std::vector<TxTypeExpressionNode*>* interfaces;
    std::vector<TxDeclarationNode*>* members;

    TxDerivedTypeNode( const TxLocation& ploc, TxTypeExpressionNode* baseType,
                       std::vector<TxTypeExpressionNode*>* interfaces, std::vector<TxDeclarationNode*>* members )
            : TxTypeExpressionNode( ploc ), baseType( baseType ), interfaces( interfaces ), members( members ) {
    }

    TxDerivedTypeNode( const TxLocation& ploc, TxTypeExpressionNode* baseType, std::vector<TxDeclarationNode*>* members )
        : TxDerivedTypeNode(ploc, baseType, new std::vector<TxTypeExpressionNode*>(), members) { }

    TxDerivedTypeNode( const TxLocation& ploc, std::vector<TxDeclarationNode*>* members )
        : TxDerivedTypeNode(ploc, nullptr, new std::vector<TxTypeExpressionNode*>(), members) { }

    virtual void set_requires_mutable( bool mut ) override;

    virtual TxDerivedTypeNode* make_ast_copy() const override {
        return new TxDerivedTypeNode( this->ploc, this->baseType->make_ast_copy(),
                                      make_node_vec_copy( this->interfaces ), make_node_vec_copy( this->members ) );
                                      //this->interfaceKW, this->mutableType );
    }

    virtual void symbol_resolution_pass() override;

    virtual void code_gen_type( LlvmGenerationContext& context ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override;
};

/** Defines a function type. */
class TxFunctionTypeNode : public TxTypeExpressionNode {
    // Note: the field names aren't part of a function's formal type definition
    // (a function type doesn't declare (create entities for) the function args)

    static TxArgTypeDefNode* make_return_field( TxTypeExpressionNode* returnType ) {
        return ( returnType ? new TxArgTypeDefNode( returnType->ploc, "$return", returnType ) : nullptr );
    }

protected:
    virtual void typeexpr_declaration_pass() override;

    virtual const TxQualType* define_type() override;

public:
    /** Indicates whether functions of this type may modify its closure when run. */
    const bool modifying;
    std::vector<TxArgTypeDefNode*>* arguments;
    TxArgTypeDefNode* returnField;

    TxFunctionTypeNode( const TxLocation& ploc, const bool modifying,
                        std::vector<TxArgTypeDefNode*>* arguments, TxTypeExpressionNode* returnType )
            : TxTypeExpressionNode( ploc ), modifying( modifying ),
              arguments( arguments ),
              returnField( make_return_field( returnType ) ) {
    }

    virtual TxFunctionTypeNode* make_ast_copy() const override {
        return new TxFunctionTypeNode( this->ploc, this->modifying, make_node_vec_copy( this->arguments ),
                                       ( this->returnField ? this->returnField->typeExpression->make_ast_copy() : nullptr ) );
    }

    virtual void symbol_resolution_pass() override {
        TxTypeExpressionNode::symbol_resolution_pass();
        for ( auto argField : *this->arguments ) {
            argField->symbol_resolution_pass();
            auto argType = argField->qualtype();
            if ( is_not_properly_concrete( this, argType->type() ) ) {
                CERROR( argField, "Function argument type is not concrete: " << argField->get_descriptor() << " : " << argType );
            }
        }
        if ( this->returnField ) {
            this->returnField->symbol_resolution_pass();
            auto retType = this->returnField->qualtype();
            if ( is_not_properly_concrete( this, retType->type() ) ) {
                CERROR( returnField, "Function return type is not concrete: " << retType );
            }
        }
    }

    virtual void code_gen_type( LlvmGenerationContext& context ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        for ( auto argField : *this->arguments )
            argField->visit_ast( visitor, thisCursor, "arg", context );
        if ( this->returnField )
            this->returnField->visit_ast( visitor, thisCursor, "return", context );
    }
};

class TxModifiableTypeNode : public TxTypeExpressionNode {
protected:
    TxModifiableTypeNode( const TxLocation& ploc, TxTypeExpressionNode* typeNode, bool reqMut )
            : TxTypeExpressionNode( ploc ), typeNode( typeNode ) {
        TxTypeExpressionNode::set_requires_mutable( reqMut );
        this->typeNode->set_requires_mutable( reqMut );
    }

    virtual void typeexpr_declaration_pass() override;

    virtual const TxQualType* define_type() override {
        auto bType = this->typeNode->resolve_type();
        if ( bType->is_modifiable() ) {
            CERROR( this, "'modifiable' specified more than once for type: " << bType );
            return bType;
        }
        if ( !bType->type()->is_mutable() )
            CERR_THROWRES( this, "Can't declare immutable type as modifiable: " << bType );
        return new TxQualType( bType->type(), true );
    }

public:
    TxTypeExpressionNode* typeNode;

    TxModifiableTypeNode( const TxLocation& ploc, TxTypeExpressionNode* typeNode )
            : TxModifiableTypeNode( ploc, typeNode, true ) {
    }

    virtual void set_interface( bool ifkw ) override {
        TxTypeExpressionNode::set_interface( ifkw );
        this->typeNode->set_interface( ifkw );
    }

    virtual void set_requires_mutable( bool mut ) override {
        // do nothing, this is implicitly true for this node type
    }

    virtual TxModifiableTypeNode* make_ast_copy() const override {
        return new TxModifiableTypeNode( this->ploc, this->typeNode->make_ast_copy() );
    }

    virtual bool is_modifiable() const { return true; }

    virtual void symbol_resolution_pass() override {
        TxTypeExpressionNode::symbol_resolution_pass();
        this->typeNode->symbol_resolution_pass();
    }

    virtual void code_gen_type( LlvmGenerationContext& context ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->typeNode->visit_ast( visitor, thisCursor, "type", context );
    }
};

/** A potentially modifiable type expression, depending on syntactic sugar rules.
 * One aim is to make these equivalent: ~[]~ElemT  ~[]ElemT  []~ElemT
 * This node should not have TxModifiableTypeNode as parent node, and vice versa. */
class TxMaybeModTypeNode : public TxModifiableTypeNode {
    bool isModifiable = false;

protected:
    virtual void typeexpr_declaration_pass() override;

    virtual const TxQualType* define_type() override {
        if ( this->isModifiable )
            return TxModifiableTypeNode::define_type();
        else
            return this->typeNode->resolve_type();
    }

public:
    TxMaybeModTypeNode( const TxLocation& ploc, TxTypeExpressionNode* baseType )
            : TxModifiableTypeNode( ploc, baseType, false ) {
    }

    virtual void set_requires_mutable( bool mut ) override {
        TxTypeExpressionNode::set_requires_mutable( mut );
        this->typeNode->set_requires_mutable( mut );
    }

    virtual TxMaybeModTypeNode* make_ast_copy() const override {
        return new TxMaybeModTypeNode( this->ploc, this->typeNode->make_ast_copy() );
    }

    virtual bool is_modifiable() const override {
        return this->isModifiable;
    }

    void set_modifiable( bool mod ) {
        ASSERT( !this->attempt_qualtype(), "Can't set modifiable after type already has been resolved in " << this );
        this->isModifiable = mod;
        this->set_requires_mutable( mod );
    }

    virtual const std::string& get_descriptor() const override {
        return this->typeNode->get_descriptor();
    }
};

/** Removes the 'modifiable' modifier on a type. Should only be relevant in combination with TYPE type parameters. */
class TxConstTypeNode : public TxTypeExpressionNode {
protected:
    virtual const TxQualType* define_type() override {
        auto qtype = this->typeNode->resolve_type();
        if ( !qtype->is_modifiable() )
            return qtype;
        else
            return new TxQualType( qtype->type(), false );
    }

public:
    TxTypeExpressionNode* typeNode;

    TxConstTypeNode( const TxLocation& ploc, TxTypeExpressionNode* typeNode )
            : TxTypeExpressionNode( ploc ), typeNode( typeNode ) {
    }

    virtual void set_interface( bool ifkw ) override {
        TxTypeExpressionNode::set_interface( ifkw );
        this->typeNode->set_interface( ifkw );
    }

    virtual void set_requires_mutable( bool mut ) override {
        // Note, will not compile for this type expression
        TxTypeExpressionNode::set_requires_mutable( mut );
        this->typeNode->set_requires_mutable( mut );
    }

    virtual TxConstTypeNode* make_ast_copy() const override {
        return new TxConstTypeNode( this->ploc, this->typeNode->make_ast_copy() );
    }

    virtual bool is_modifiable() const { return false; }

    virtual void symbol_resolution_pass() override {
        TxTypeExpressionNode::symbol_resolution_pass();
        this->typeNode->symbol_resolution_pass();
    }

    virtual void code_gen_type( LlvmGenerationContext& context ) const override;

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        this->typeNode->visit_ast( visitor, thisCursor, "type", context );
    }
};
