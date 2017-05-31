#include "builtin_types.hpp"

#include "util/assert.hpp"

#include "ast.hpp"

#include "llvm_generator.hpp"

/*--- statically allocated built-in type objects ---*/

std::vector<std::string> BUILTIN_TYPE_NAMES = {
                                                "Any",
                                                "Void",
                                                "Interface",
                                                "Elementary",
                                                "Scalar",
                                                "Integer",
                                                "Signed",
                                                "Byte",
                                                "Short",
                                                "Int",
                                                "Long",
                                                "Unsigned",
                                                "UByte",
                                                "UShort",
                                                "UInt",
                                                "ULong",
                                                "Floatingpoint",
                                                "Half",
                                                "Float",
                                                "Double",
                                                "Bool",
                                                "Reference",
                                                "Array",
                                                "Function",
                                                "Tuple",
};

/** Helper function that makes a deep-copy of a vector of nodes to an initializer list. */
template<class N>
std::vector<N*> make_node_vec_copy( const std::vector<N*>& nodeVec ) {
    std::vector<N*> copyVec( nodeVec.size() );
    std::transform( nodeVec.cbegin(), nodeVec.cend(), copyVec.begin(),
                    []( N* n ) -> N* {return n->make_ast_copy();} );
    return copyVec;
}

/*--- private classes providing indirection for fetching the built-in type objects ---*/

class TxBuiltinFieldDefNode final : public TxFieldDefiningNode {
    const TxField* builtinField;

protected:
    virtual const TxType* define_type() override {
        return this->builtinField->get_type();
    }

    virtual const TxField* define_field() override {
        return this->builtinField;
    }

public:

    TxBuiltinFieldDefNode( const TxLocation& parseLocation )
            : TxFieldDefiningNode( parseLocation ), builtinField() {
    }

    virtual TxFieldDefiningNode* make_ast_copy() const override {
        ASSERT( false, "Can't make AST copy of a TxBuiltinFieldDefNode: " << this );
        return nullptr;
    }

    void set_field( const TxField* field ) {
        this->builtinField = field;
        this->set_context( LexicalContext( field->get_symbol()->get_outer(), nullptr, false, nullptr ) );  // emulate declaration pass
        this->resolve_field();  // auto-resolves
    }

    virtual const TxExpressionNode* get_init_expression() const {
        return nullptr;
    }

    virtual void symbol_resolution_pass() override { }

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
    }
};

/*------ built-in type defining AST nodes ------*/

/** Abstract superclass for the built-in type defining nodes. */
class TxBuiltinTypeDefiningNode : public TxTypeExpressionNode {
    void merge_builtin_type_definers( TxDerivedTypeNode* sourcecodeDefiner ) {
        ASSERT( this->is_context_set(), "Builtin type node hasn't run declaration pass: " << this );
        ASSERT( !this->attempt_get_type(), "Builtin type already resolved: " << this );
        sourcecodeDefiner->set_builtin_type_definer( this );
        this->sourcecodeDefiner = sourcecodeDefiner;
    }

    friend void merge_builtin_type_definers( TxDerivedTypeNode* sourcecodeDefiner, TxTypeDefiningNode* builtinDefiner );

protected:
    const BuiltinTypeId builtinTypeId;
    TxBuiltinTypeDefiningNode const * const original;

    /** Specifies the base type of this type (null for the Any type). */
    TxTypeExpressionNode* const baseTypeNode;
    /** Declarations within this type's namespace. */
    std::vector<TxDeclarationNode*> declNodes;

    /** behavior defined via source code */
    TxDerivedTypeNode* sourcecodeDefiner = nullptr;

    /** for use by make_ast_copy() in subclasses */
    TxBuiltinTypeDefiningNode( const TxLocation& parseLocation, const TxBuiltinTypeDefiningNode* original,
                               TxTypeExpressionNode* baseTypeNode, const std::vector<TxDeclarationNode*>& declNodes,
                               TxDerivedTypeNode* sourcecodeDefiner )
            : TxTypeExpressionNode( parseLocation ), builtinTypeId( TXBT_NOTSET ), original( original ),
              baseTypeNode( baseTypeNode ), declNodes( declNodes ), sourcecodeDefiner( sourcecodeDefiner ) {
        if (sourcecodeDefiner)
            sourcecodeDefiner->set_builtin_type_definer( this );
    }

    virtual void typeexpr_declaration_pass() override {
        if ( this->sourcecodeDefiner ) {
            // "pass through" entity declaration to the source code definer
            this->sourcecodeDefiner->set_declaration( this->get_declaration() );
        }
    }

    virtual const TxType* define_type() override final {
// This used to be necessary but hindered providing members to Array and Ref in source code:
//        if ( this->original ) {  // true when this is a reinterpreted copy
//            // Note: This applies to Ref and Array specializations and deviates from reinterpretation of user types:
//            //   Specializations of user types will not have the generic base type as their base type,
//            //   instead they will have the generic base type's parent as their base type.
//            //   Here that would be  this->baseTypeNode->resolve_type()  instead of  this->original->get_type() .
//            //   That would however not work with the current type class implementation (TxReferenceType and TxArrayType).
//            return this->registry().make_type_entity( this->registry().make_actual_type( this->get_declaration(),
//                                                                                         this->original->get_type()->type() ) );
//        }
        auto actType = this->define_builtin_type();
        actType->formalTypeId = builtinTypeId;
        this->registry().add_type( actType );
        return this->registry().make_type_entity( actType );
    }

    virtual TxActualType* define_builtin_type() = 0;

    /** helper method for subclasses that constructs a vector of TxTypeSpecialization of this instance's interface expressions */
    std::vector<TxTypeSpecialization> resolve_interface_specs() const {
        std::vector<TxTypeSpecialization> ifSpecs;
        if ( this->sourcecodeDefiner ) {
            for ( auto ifDef : *this->sourcecodeDefiner->interfaces )
                ifSpecs.emplace_back( ifDef->resolve_type()->type() );
        }
        return ifSpecs;
    }

//    /** Returns true if this type expression requires the produced type to be mutable. Used by subclasses upon type creation. */
//    bool requires_mutable_type() const {
//    }

public:
    TxBuiltinTypeDefiningNode( const TxLocation& parseLocation, BuiltinTypeId builtinTypeId,
                               TxTypeExpressionNode* baseTypeNode,
                               const std::vector<TxDeclarationNode*>& declNodes )
            : TxTypeExpressionNode( parseLocation ), builtinTypeId( builtinTypeId ), original( nullptr ),
              baseTypeNode( baseTypeNode ),
              declNodes( declNodes ) {
    }

    virtual TxBuiltinTypeDefiningNode* make_ast_copy() const override {
        // (only valid for Ref and Array, which override this method)
        ASSERT( false, "Can't make AST copy of built-in type definer " << this );
        return nullptr;
    }

    virtual std::string get_auto_type_name() const override final {
        return this->get_declaration()->get_unique_full_name();
    }

    virtual void symbol_resolution_pass() override {
        TxTypeExpressionNode::symbol_resolution_pass();
        for ( auto decl : this->declNodes )
            decl->symbol_resolution_pass();
        if ( this->sourcecodeDefiner )
            this->sourcecodeDefiner->symbol_resolution_pass();
    }

    virtual void code_gen_type( LlvmGenerationContext& context ) const override {
        if ( this->baseTypeNode )
            this->baseTypeNode->code_gen_type( context );
        for ( auto decl : this->declNodes )
            decl->code_gen( context );
        if ( this->sourcecodeDefiner )
            this->sourcecodeDefiner->code_gen_type( context );
    }

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        if ( this->baseTypeNode )
            this->baseTypeNode->visit_ast( visitor, thisCursor, "basetype", context );
        for ( auto decl : this->declNodes )
            decl->visit_ast( visitor, thisCursor, "decl", context );
        if ( this->sourcecodeDefiner )
            this->sourcecodeDefiner->visit_ast( visitor, thisCursor, "source", context );
    }
};

void merge_builtin_type_definers( TxDerivedTypeNode* sourcecodeDefiner, TxTypeDefiningNode* builtinDefiner ) {
    auto builtinNode = dynamic_cast<TxBuiltinTypeDefiningNode*>( builtinDefiner );
    if ( !builtinNode )
        THROW_LOGIC( "Expected builtin type definer to be of type TxBuiltinTypeDefiningNode: " << builtinDefiner );
    builtinNode->merge_builtin_type_definers( sourcecodeDefiner );
}

//void merge_declaration_nodes( TxTypeDefiningNode* builtinDefiner, const std::vector<TxDeclarationNode*>& declNodes ) {
//    auto builtinNode = dynamic_cast<TxBuiltinTypeDefiningNode*>( builtinDefiner );
//    if (! builtinNode)
//        THROW_LOGIC("Expected builtin type definer to be of type TxBuiltinTypeDefiningNode: " << builtinDefiner);
//    ASSERT(builtinNode->is_context_set(), "Expected builtinNode to have run declaration pass: " << builtinNode);
//    for (auto declNode : declNodes) {
//        ASSERT(declNode->is_context_set(), "Expected merged decl-node to have run declaration pass: " << declNode);
//        //declNode->symbol_declaration_pass( builtinNode->context() );
//        builtinNode->add_declaration_node( declNode );
//    }
//}

/** Single-purpose node that defines the Any root type. */
class TxAnyTypeDefNode final : public TxBuiltinTypeDefiningNode {
    /** Used solely for the Any root type object. */
    class TxAnyType final : public TxActualType {
        TxAnyType( const TxTypeDeclaration* declaration, const TxTypeSpecialization& baseTypeSpec )
                : TxActualType( TXTC_ANY, declaration, baseTypeSpec, true ) {
        }

        TxAnyType* make_specialized_type( const TxTypeDeclaration* declaration, const TxTypeSpecialization& baseTypeSpec,
                                          bool mutableType, const std::vector<TxTypeSpecialization>& interfaces ) const override {
            // Note: Only for equivalent derivations - e.g. empty, 'modifiable', and GENPARAM constraints.
            if ( !dynamic_cast<const TxAnyType*>( baseTypeSpec.type ) )
                throw std::logic_error( "Specified a base type for TxAnyType that was not a TxAnyType: " + baseTypeSpec.type->str() );
            return new TxAnyType( declaration, baseTypeSpec );
        }

    public:
        TxAnyType( const TxTypeDeclaration* declaration )
                : TxActualType( TXTC_ANY, declaration, true ) {
        }

        virtual llvm::Type* make_llvm_type( LlvmGenerationContext& context ) const override {
            //LOG_TRACE(context.LOGGER(), "LLVM type for abstract type " << this << " is VOID");
            return llvm::StructType::get( context.llvmContext );  // abstract type
        }
    };

protected:
    virtual TxActualType* define_builtin_type() override {
        return new TxAnyType( this->get_declaration() );
    }

public:
    TxAnyTypeDefNode( const TxLocation& parseLocation, const std::vector<TxDeclarationNode*>& declNodes = { } )
            : TxBuiltinTypeDefiningNode( parseLocation, TXBT_ANY, nullptr, declNodes ) {
    }
};

/** Single-purpose node that defines the Void type. */
class TxVoidTypeDefNode final : public TxBuiltinTypeDefiningNode {
    /** Used solely for the Void type object. */
    class TxVoidType final : public TxActualType {
        TxVoidType* make_specialized_type( const TxTypeDeclaration* declaration, const TxTypeSpecialization& baseTypeSpec,
                                           bool mutableType, const std::vector<TxTypeSpecialization>& interfaces ) const override {
            throw std::logic_error( "Can't specialize Void" );
        }

    public:
        TxVoidType( const TxTypeDeclaration* declaration )
                : TxActualType( TXTC_VOID, declaration, false ) {
        }

        virtual llvm::Type* make_llvm_type( LlvmGenerationContext& context ) const override {
            LOG_TRACE( context.LOGGER(), "LLVM type for abstract type " << this << " is VOID" );
            return context.get_voidT();
        }
    };

protected:
    virtual TxActualType* define_builtin_type() override {
        return new TxVoidType( this->get_declaration() );
    }

public:
    TxVoidTypeDefNode( const TxLocation& parseLocation, TxTypeExpressionNode* baseTypeNode, const std::vector<TxDeclarationNode*>& declNodes = { } )
            : TxBuiltinTypeDefiningNode( parseLocation, TXBT_VOID, baseTypeNode, declNodes ) {
    }
};

/** Used to define the built-in types' abstract base types. */
class TxBuiltinAbstractTypeDefNode final : public TxBuiltinTypeDefiningNode {
    /** Used for the built-in types' abstract base types. */
    class TxBuiltinBaseType final : public TxActualType {
        TxBuiltinBaseType* make_specialized_type( const TxTypeDeclaration* declaration, const TxTypeSpecialization& baseTypeSpec,
                                                  bool mutableType, const std::vector<TxTypeSpecialization>& interfaces ) const override {
            if ( !dynamic_cast<const TxBuiltinBaseType*>( baseTypeSpec.type ) )
                throw std::logic_error( "Specified a base type for TxBuiltinBaseType that was not a TxBuiltinBaseType: " + baseTypeSpec.type->str() );
            return new TxBuiltinBaseType( baseTypeSpec.type->get_type_class(), declaration, baseTypeSpec, interfaces );
        }

    public:
        TxBuiltinBaseType( TxTypeClass typeClass, const TxTypeDeclaration* declaration, const TxTypeSpecialization& baseTypeSpec,
                           const std::vector<TxTypeSpecialization>& interfaces )
                : TxActualType( typeClass, declaration, baseTypeSpec, true, interfaces ) {
        }

        virtual llvm::Type* make_llvm_type( LlvmGenerationContext& context ) const override {
            LOG_TRACE( context.LOGGER(), "LLVM type for abstract type " << this << " is VOID" );
            return context.get_voidT();
        }
    };

protected:
    virtual TxActualType* define_builtin_type() override {
        auto baseTypeSpec = TxTypeSpecialization( this->baseTypeNode->resolve_type()->type() );
        auto ifSpecs = this->resolve_interface_specs();
        return new TxBuiltinBaseType( this->typeClass, this->get_declaration(), baseTypeSpec, ifSpecs );
    }
public:
    const TxTypeClass typeClass;
    TxBuiltinAbstractTypeDefNode( const TxLocation& parseLocation, BuiltinTypeId builtinTypeId, TxTypeExpressionNode* baseTypeNode,
                                  TxTypeClass typeClass,
                                  const std::vector<TxDeclarationNode*>& declNodes )
            : TxBuiltinTypeDefiningNode( parseLocation, builtinTypeId, baseTypeNode, declNodes ), typeClass( typeClass ) {
    }
};

class TxIntegerTypeDefNode final : public TxBuiltinTypeDefiningNode {
protected:
    virtual TxActualType* define_builtin_type() override {
        auto baseTypeSpec = TxTypeSpecialization( this->baseTypeNode->resolve_type()->type() );
        auto ifSpecs = this->resolve_interface_specs();
        return new TxIntegerType( this->get_declaration(), baseTypeSpec, ifSpecs, this->size, this->sign );
    }
public:
    const int size;
    const bool sign;
    TxIntegerTypeDefNode( const TxLocation& parseLocation, BuiltinTypeId builtinTypeId, TxTypeExpressionNode* baseTypeNode, int size, bool sign,
                          const std::vector<TxDeclarationNode*>& declNodes )
            : TxBuiltinTypeDefiningNode( parseLocation, builtinTypeId, baseTypeNode, declNodes ), size( size ), sign( sign ) {
    }
};

class TxFloatingTypeDefNode final : public TxBuiltinTypeDefiningNode {
protected:
    virtual TxActualType* define_builtin_type() override {
        auto baseTypeSpec = TxTypeSpecialization( this->baseTypeNode->resolve_type()->type() );
        auto ifSpecs = this->resolve_interface_specs();
        return new TxFloatingType( this->get_declaration(), baseTypeSpec, ifSpecs, this->size );
    }
public:
    const int size;
    TxFloatingTypeDefNode( const TxLocation& parseLocation, BuiltinTypeId builtinTypeId, TxTypeExpressionNode* baseTypeNode, int size,
                           const std::vector<TxDeclarationNode*>& declNodes )
            : TxBuiltinTypeDefiningNode( parseLocation, builtinTypeId, baseTypeNode, declNodes ), size( size ) {
    }
};

class TxBoolTypeDefNode final : public TxBuiltinTypeDefiningNode {
protected:
    virtual TxActualType* define_builtin_type() override {
        auto baseTypeSpec = TxTypeSpecialization( this->baseTypeNode->resolve_type()->type() );
        auto ifSpecs = this->resolve_interface_specs();
        return new TxBoolType( this->get_declaration(), baseTypeSpec, ifSpecs );
    }
public:
    TxBoolTypeDefNode( const TxLocation& parseLocation, TxTypeExpressionNode* baseTypeNode,
                       const std::vector<TxDeclarationNode*>& declNodes )
            : TxBuiltinTypeDefiningNode( parseLocation, TXBT_BOOL, baseTypeNode, declNodes ) {
    }
};

class TxTupleTypeDefNode final : public TxBuiltinTypeDefiningNode {
protected:
    virtual TxActualType* define_builtin_type() override {
        auto baseTypeSpec = TxTypeSpecialization( this->baseTypeNode->resolve_type()->type() );
        auto ifSpecs = this->resolve_interface_specs();
        return new TxTupleType( this->get_declaration(), baseTypeSpec, ifSpecs, this->requires_mutable_type() );
    }
public:
    TxTupleTypeDefNode( const TxLocation& parseLocation, TxTypeExpressionNode* baseTypeNode,
                        const std::vector<TxDeclarationNode*>& declNodes )
            : TxBuiltinTypeDefiningNode( parseLocation, TXBT_TUPLE, baseTypeNode, declNodes ) {
    }
};

class TxInterfaceTypeDefNode final : public TxBuiltinTypeDefiningNode {
protected:
    virtual TxActualType* define_builtin_type() override {
        auto baseTypeSpec = TxTypeSpecialization( this->baseTypeNode->resolve_type()->type() );
        auto ifSpecs = this->resolve_interface_specs();
        return new TxInterfaceType( this->get_declaration(), baseTypeSpec, ifSpecs );
    }
public:
    TxInterfaceTypeDefNode( const TxLocation& parseLocation, TxTypeExpressionNode* baseTypeNode,
                            const std::vector<TxDeclarationNode*>& declNodes )
            : TxBuiltinTypeDefiningNode( parseLocation, TXBT_INTERFACE, baseTypeNode, declNodes ) {
    }
};

class TxRefTypeDefNode final : public TxBuiltinTypeDefiningNode {
    TxRefTypeDefNode( const TxLocation& parseLocation, const TxRefTypeDefNode* original,
                      TxTypeExpressionNode* baseTypeNode, const std::vector<TxDeclarationNode*>& declNodes, TxDerivedTypeNode* sourcecodeDefiner )
            : TxBuiltinTypeDefiningNode( parseLocation, original, baseTypeNode, declNodes, sourcecodeDefiner ) {
    }
protected:
    virtual TxActualType* define_builtin_type() override {
        auto baseTypeSpec = TxTypeSpecialization( this->baseTypeNode->resolve_type()->type() );
        auto ifSpecs = this->resolve_interface_specs();
        return new TxReferenceType( this->get_declaration(), baseTypeSpec, ifSpecs );
    }
public:
    TxRefTypeDefNode( const TxLocation& parseLocation, TxTypeExpressionNode* baseTypeNode,
                      const std::vector<TxDeclarationNode*>& declNodes,
                      const TxRefTypeDefNode* original = nullptr )
            : TxBuiltinTypeDefiningNode( parseLocation, TXBT_REFERENCE, baseTypeNode, declNodes ) {
    }

    virtual TxRefTypeDefNode* make_ast_copy() const override {
        return new TxRefTypeDefNode( this->parseLocation, this, this->baseTypeNode->make_ast_copy(), make_node_vec_copy( this->declNodes ),
                                     ( this->sourcecodeDefiner ? this->sourcecodeDefiner->make_ast_copy() : nullptr ) );
    }
};

class TxArrayTypeDefNode final : public TxBuiltinTypeDefiningNode {
    TxArrayTypeDefNode( const TxLocation& parseLocation, const TxArrayTypeDefNode* original,
                        TxTypeExpressionNode* baseTypeNode, const std::vector<TxDeclarationNode*>& declNodes, TxDerivedTypeNode* sourcecodeDefiner )
            : TxBuiltinTypeDefiningNode( parseLocation, original, baseTypeNode, declNodes, sourcecodeDefiner ) {
    }
protected:
    virtual TxActualType* define_builtin_type() override {
        auto baseTypeSpec = TxTypeSpecialization( this->baseTypeNode->resolve_type()->type() );
        auto ifSpecs = this->resolve_interface_specs();
        return new TxArrayType( this->get_declaration(), baseTypeSpec, this->requires_mutable_type(), ifSpecs );
    }
public:
    TxArrayTypeDefNode( const TxLocation& parseLocation, TxTypeExpressionNode* baseTypeNode,
                        const std::vector<TxDeclarationNode*>& declNodes,
                        const TxArrayTypeDefNode* original = nullptr )
            : TxBuiltinTypeDefiningNode( parseLocation, TXBT_ARRAY, baseTypeNode, declNodes ) {
    }

    virtual TxArrayTypeDefNode* make_ast_copy() const override {
        return new TxArrayTypeDefNode( this->parseLocation, this, this->baseTypeNode->make_ast_copy(), make_node_vec_copy( this->declNodes ),
                                       ( this->sourcecodeDefiner ? this->sourcecodeDefiner->make_ast_copy() : nullptr ) );
    }
};

/*----- built-in constructor / initializer type defining AST nodes -----*/

class TxBuiltinConstructorTypeDefNode : public TxFunctionTypeNode {
protected:

public:
    TxBuiltinConstructorTypeDefNode( const TxLocation& parseLocation, std::vector<TxArgTypeDefNode*>* arguments, TxTypeExpressionNode* returnType )
            : TxFunctionTypeNode( parseLocation, false, arguments, returnType ) {
    }

    virtual TxBuiltinConstructorTypeDefNode* make_ast_copy() const override = 0;
};

class TxDefConstructorTypeDefNode final : public TxBuiltinConstructorTypeDefNode {
protected:
    TxExpressionNode* initExprNode;

    virtual const TxType* define_type() override {
        auto actType = new TxBuiltinDefaultConstructorType( this->get_declaration(), this->registry().get_builtin_type( TXBT_FUNCTION )->type(),
                                                            this->returnField->resolve_type()->type(),
                                                            initExprNode );
        return this->registry().make_type_entity( actType );
    }

public:
    TxDefConstructorTypeDefNode( const TxLocation& parseLocation, TxTypeExpressionNode* returnTypeNode, TxExpressionNode* initExprNode )
            : TxBuiltinConstructorTypeDefNode( parseLocation, new std::vector<TxArgTypeDefNode*>(), returnTypeNode ), initExprNode( initExprNode ) {
    }

    virtual TxDefConstructorTypeDefNode* make_ast_copy() const override {
        return new TxDefConstructorTypeDefNode( this->parseLocation, this->returnField->typeExpression->make_ast_copy(),
                                                initExprNode->make_ast_copy() );
    }

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
        TxBuiltinConstructorTypeDefNode::visit_descendants( visitor, thisCursor, role, context );
        this->initExprNode->visit_ast( visitor, thisCursor, "initializer", context );
    }
};

class TxConvConstructorTypeDefNode final : public TxBuiltinConstructorTypeDefNode {
protected:
    virtual const TxType* define_type() override {
        auto actType = new TxBuiltinConversionFunctionType( this->get_declaration(), this->registry().get_builtin_type( TXBT_FUNCTION )->type(),
                                                            this->arguments->at( 0 )->resolve_type()->type(),
                                                            this->returnField->resolve_type()->type() );
        return this->registry().make_type_entity( actType );
    }

public:
    TxConvConstructorTypeDefNode( const TxLocation& parseLocation, TxArgTypeDefNode* fromTypeArg, TxTypeExpressionNode* returnTypeNode )
            : TxBuiltinConstructorTypeDefNode( parseLocation, new std::vector<TxArgTypeDefNode*>( { fromTypeArg } ), returnTypeNode ) {
    }

    virtual TxConvConstructorTypeDefNode* make_ast_copy() const override {
        return new TxConvConstructorTypeDefNode( this->parseLocation, this->arguments->at( 0 )->make_ast_copy(),
                                                 this->returnField->typeExpression->make_ast_copy() );
    }
};

class TxArrayConstructorTypeDefNode final : public TxBuiltinConstructorTypeDefNode {
protected:
    virtual const TxType* define_type() override {
        auto actType = new TxBuiltinArrayInitializerType( this->get_declaration(), this->registry().get_builtin_type( TXBT_FUNCTION )->type(),
                                                          this->arguments->at( 0 )->resolve_type()->type(),
                                                          this->returnField->resolve_type()->type() );
        return this->registry().make_type_entity( actType );
    }

public:
    TxArrayConstructorTypeDefNode( const TxLocation& parseLocation, TxArgTypeDefNode* fromTypeArg, TxTypeExpressionNode* returnTypeNode )
            : TxBuiltinConstructorTypeDefNode( parseLocation, new std::vector<TxArgTypeDefNode*>( { fromTypeArg } ), returnTypeNode ) {
    }

    virtual TxArrayConstructorTypeDefNode* make_ast_copy() const override {
        return new TxArrayConstructorTypeDefNode( this->parseLocation, this->arguments->at( 0 )->make_ast_copy(),
                                                  this->returnField->typeExpression->make_ast_copy() );
    }
};

/*----- helper functions creating the built-in elementary types' declarations and constructors -----*/

static TxTypeDeclNode* make_builtin_abstract( const TxLocation& parseLoc, TxTypeClass typeClass, BuiltinTypeId id, BuiltinTypeId parentId ) {
    auto baseTypeNode = new TxNamedTypeNode( parseLoc, BUILTIN_TYPE_NAMES[parentId] );
    auto typeDecl = new TxTypeDeclNode( parseLoc, TXD_PUBLIC | TXD_BUILTIN | TXD_ABSTRACT, BUILTIN_TYPE_NAMES[id], nullptr,
                                        new TxBuiltinAbstractTypeDefNode( parseLoc, id, baseTypeNode, typeClass, { } ), false, true );
    return typeDecl;
}

static TxTypeDeclNode* make_builtin_integer( const TxLocation& parseLoc, BuiltinTypeId id, BuiltinTypeId parentId,
                                             std::vector<std::vector<TxDeclarationNode*>>& constructors,
                                             int size, bool sign ) {
    auto baseTypeNode = new TxNamedTypeNode( parseLoc, BUILTIN_TYPE_NAMES[parentId] );
    auto typeDecl = new TxTypeDeclNode( parseLoc, TXD_PUBLIC | TXD_BUILTIN | TXD_FINAL, BUILTIN_TYPE_NAMES[id], nullptr,
                                        new TxIntegerTypeDefNode( parseLoc, id, baseTypeNode, size, sign, constructors[id] ), false, true );
    return typeDecl;
}

static TxTypeDeclNode* make_builtin_floating( const TxLocation& parseLoc, BuiltinTypeId id, BuiltinTypeId parentId,
                                              std::vector<std::vector<TxDeclarationNode*>>& constructors,
                                              int size ) {
    auto baseTypeNode = new TxNamedTypeNode( parseLoc, BUILTIN_TYPE_NAMES[parentId] );
    auto typeDecl = new TxTypeDeclNode( parseLoc, TXD_PUBLIC | TXD_BUILTIN | TXD_FINAL, BUILTIN_TYPE_NAMES[id], nullptr,
                                        new TxFloatingTypeDefNode( parseLoc, id, baseTypeNode, size, constructors[id] ), false, true );
    return typeDecl;
}

static TxFieldDeclNode* make_default_initializer( const TxLocation& loc, BuiltinTypeId toTypeId, TxExpressionNode* initializerExpr ) {
    auto toTypeNode = new TxNamedTypeNode( loc, BUILTIN_TYPE_NAMES[toTypeId] );
    return new TxFieldDeclNode( loc, TXD_PUBLIC | TXD_STATIC | TXD_BUILTIN | TXD_INITIALIZER,
                                new TxFieldDefNode( loc, CONSTR_IDENT,
                                                    new TxDefConstructorTypeDefNode( loc, toTypeNode, initializerExpr ),
                                                    nullptr ),  // no function body, initialization is inlined
//                        new TxLambdaExprNode( loc,
//                                new TxDefConstructorTypeDefNode( loc, toTypeNode, initializerExpr ),
//                                new TxSuiteNode( loc, new std::vector<TxStatementNode*>( {
//                                                            new TxReturnStmtNode( loc, initializerExpr )
//                                                        } ) ),
//                                false )  // not method syntax since elementary types' initializers are inlineable, pure functions
//                        ),
                                false );  // not method syntax since elementary types' initializers are inlineable, pure functions
}

static TxFieldDeclNode* make_conversion_initializer( const TxLocation& loc, BuiltinTypeId fromTypeId, BuiltinTypeId toTypeId ) {
    auto toTypeNode = new TxNamedTypeNode( loc, BUILTIN_TYPE_NAMES[toTypeId] );
    auto fromTypeNode = new TxNamedTypeNode( loc, BUILTIN_TYPE_NAMES[fromTypeId] );
    return new TxFieldDeclNode(
            loc, TXD_PUBLIC | TXD_STATIC | TXD_BUILTIN | TXD_INITIALIZER,
            new TxFieldDefNode( loc, CONSTR_IDENT,
                                new TxConvConstructorTypeDefNode( loc, new TxArgTypeDefNode( loc, "arg", fromTypeNode ), toTypeNode ),
                                nullptr ),  // no function body, initialization is inlined
            false );  // not method syntax since elementary types' initializers are inlineable, pure functions
}

static std::vector<TxDeclarationNode*> make_array_constructors( const TxLocation& loc ) {
    std::vector<TxDeclarationNode*> constructors;
    auto argNode = new TxArgTypeDefNode( loc, "val", new TxNamedTypeNode( loc, "Self" ) );
    auto returnTypeNode = new TxNamedTypeNode( loc, "Self" );
    constructors.push_back( new TxFieldDeclNode( loc, TXD_PUBLIC | TXD_STATIC | TXD_BUILTIN | TXD_INITIALIZER,
                                                 new TxFieldDefNode( loc, CONSTR_IDENT,
                                                                     new TxArrayConstructorTypeDefNode( loc, argNode, returnTypeNode ),
                                                                     nullptr ),  // no function body, initialization is inlined
                                                 false ) );  // not method syntax since this initializer is an inlineable, pure function
    return constructors;
}

TxParsingUnitNode* BuiltinTypes::createTxModuleAST() {
    auto & loc = this->builtinLocation;

    { // create the Any root type:
        auto anyTypeDecl = new TxTypeDeclNode( loc, TXD_PUBLIC | TXD_BUILTIN | TXD_ABSTRACT, "Any", nullptr,
                                               new TxAnyTypeDefNode( loc ), false, true );
        this->builtinTypes[TXBT_ANY] = anyTypeDecl;
        // candidate members of the Any root type:
        // public static _typeid() UInt
        // public final _address() ULong
    }

    { // create the Void type:
        auto voidBaseTypeNode = new TxNamedTypeNode( loc, BUILTIN_TYPE_NAMES[TXBT_ANY] );
        auto voidTypeDecl = new TxTypeDeclNode( loc, TXD_PUBLIC | TXD_BUILTIN | TXD_ABSTRACT | TXD_FINAL, "Void", nullptr,
                                                new TxVoidTypeDefNode( loc, voidBaseTypeNode ), false, false );
        this->builtinTypes[TXBT_VOID] = voidTypeDecl;
    }

    // default initializers for elementary, concrete built-ins:
    std::vector<std::vector<TxDeclarationNode*>> constructors( BuiltinTypeId_COUNT );
    constructors[TXBT_BOOL].push_back( make_default_initializer( loc, TXBT_BOOL, new TxBoolLitNode( loc, false ) ) );
    constructors[TXBT_BYTE].push_back( make_default_initializer( loc, TXBT_BYTE, new TxIntegerLitNode( loc, 0, true, TXBT_BYTE ) ) );
    constructors[TXBT_SHORT].push_back( make_default_initializer( loc, TXBT_SHORT, new TxIntegerLitNode( loc, 0, true, TXBT_SHORT ) ) );
    constructors[TXBT_INT].push_back( make_default_initializer( loc, TXBT_INT, new TxIntegerLitNode( loc, 0, true, TXBT_INT ) ) );
    constructors[TXBT_LONG].push_back( make_default_initializer( loc, TXBT_LONG, new TxIntegerLitNode( loc, 0, true, TXBT_LONG ) ) );
    constructors[TXBT_UBYTE].push_back( make_default_initializer( loc, TXBT_UBYTE, new TxIntegerLitNode( loc, 0, false, TXBT_UBYTE ) ) );
    constructors[TXBT_USHORT].push_back( make_default_initializer( loc, TXBT_USHORT, new TxIntegerLitNode( loc, 0, false, TXBT_USHORT ) ) );
    constructors[TXBT_UINT].push_back( make_default_initializer( loc, TXBT_UINT, new TxIntegerLitNode( loc, 0, false, TXBT_UINT ) ) );
    constructors[TXBT_ULONG].push_back( make_default_initializer( loc, TXBT_ULONG, new TxIntegerLitNode( loc, 0, false, TXBT_ULONG ) ) );
    constructors[TXBT_HALF].push_back( make_default_initializer( loc, TXBT_HALF, new TxFloatingLitNode( loc, TXBT_HALF ) ) );
    constructors[TXBT_FLOAT].push_back( make_default_initializer( loc, TXBT_FLOAT, new TxFloatingLitNode( loc, TXBT_FLOAT ) ) );
    constructors[TXBT_DOUBLE].push_back( make_default_initializer( loc, TXBT_DOUBLE, new TxFloatingLitNode( loc, TXBT_DOUBLE ) ) );

    // built-in conversion-initializers between the concrete elementary types (BOOL and the scalar types):
    const BuiltinTypeId CONCRETE_ELEM_TYPE_IDS[] = {
                                                     TXBT_BOOL,
                                                     TXBT_BYTE,
                                                     TXBT_SHORT,
                                                     TXBT_INT,
                                                     TXBT_LONG,
                                                     TXBT_UBYTE,
                                                     TXBT_USHORT,
                                                     TXBT_UINT,
                                                     TXBT_ULONG,
                                                     TXBT_HALF,
                                                     TXBT_FLOAT,
                                                     TXBT_DOUBLE,
    };
    for ( auto toTypeId : CONCRETE_ELEM_TYPE_IDS ) {
        for ( auto fromTypeId : CONCRETE_ELEM_TYPE_IDS )
            constructors[toTypeId].push_back( make_conversion_initializer( loc, fromTypeId, toTypeId ) );
    }

    // create the built-in abstract base types:
    this->builtinTypes[TXBT_ELEMENTARY] = make_builtin_abstract( loc, TXTC_ELEMENTARY, TXBT_ELEMENTARY, TXBT_ANY );
    this->builtinTypes[TXBT_SCALAR] = make_builtin_abstract( loc, TXTC_ELEMENTARY, TXBT_SCALAR, TXBT_ELEMENTARY );
    this->builtinTypes[TXBT_INTEGER] = make_builtin_abstract( loc, TXTC_ELEMENTARY, TXBT_INTEGER, TXBT_SCALAR );
    this->builtinTypes[TXBT_SIGNED] = make_builtin_abstract( loc, TXTC_ELEMENTARY, TXBT_SIGNED, TXBT_INTEGER );
    this->builtinTypes[TXBT_UNSIGNED] = make_builtin_abstract( loc, TXTC_ELEMENTARY, TXBT_UNSIGNED, TXBT_INTEGER );
    this->builtinTypes[TXBT_FLOATINGPOINT] = make_builtin_abstract( loc, TXTC_ELEMENTARY, TXBT_FLOATINGPOINT, TXBT_SCALAR );

    // create the built-in concrete scalar types:
    this->builtinTypes[TXBT_BYTE] = make_builtin_integer( loc, TXBT_BYTE, TXBT_SIGNED, constructors, 1, true );
    this->builtinTypes[TXBT_SHORT] = make_builtin_integer( loc, TXBT_SHORT, TXBT_SIGNED, constructors, 2, true );
    this->builtinTypes[TXBT_INT] = make_builtin_integer( loc, TXBT_INT, TXBT_SIGNED, constructors, 4, true );
    this->builtinTypes[TXBT_LONG] = make_builtin_integer( loc, TXBT_LONG, TXBT_SIGNED, constructors, 8, true );
    this->builtinTypes[TXBT_UBYTE] = make_builtin_integer( loc, TXBT_UBYTE, TXBT_UNSIGNED, constructors, 1, false );
    this->builtinTypes[TXBT_USHORT] = make_builtin_integer( loc, TXBT_USHORT, TXBT_UNSIGNED, constructors, 2, false );
    this->builtinTypes[TXBT_UINT] = make_builtin_integer( loc, TXBT_UINT, TXBT_UNSIGNED, constructors, 4, false );
    this->builtinTypes[TXBT_ULONG] = make_builtin_integer( loc, TXBT_ULONG, TXBT_UNSIGNED, constructors, 8, false );
    this->builtinTypes[TXBT_HALF] = make_builtin_floating( loc, TXBT_HALF, TXBT_FLOATINGPOINT, constructors, 2 );
    this->builtinTypes[TXBT_FLOAT] = make_builtin_floating( loc, TXBT_FLOAT, TXBT_FLOATINGPOINT, constructors, 4 );
    this->builtinTypes[TXBT_DOUBLE] = make_builtin_floating( loc, TXBT_DOUBLE, TXBT_FLOATINGPOINT, constructors, 8 );

    // create the boolean type:
    this->builtinTypes[TXBT_BOOL] = new TxTypeDeclNode(
            loc, TXD_PUBLIC | TXD_BUILTIN | TXD_FINAL, "Bool", nullptr,
            new TxBoolTypeDefNode( loc, new TxNamedTypeNode( loc, "Elementary" ), constructors[TXBT_BOOL] ), false, true );

    // create the function base type:
    this->builtinTypes[TXBT_FUNCTION] = new TxTypeDeclNode(
            loc, TXD_PUBLIC | TXD_BUILTIN | TXD_ABSTRACT, "Function", nullptr,
            new TxBuiltinAbstractTypeDefNode( loc, TXBT_FUNCTION, new TxNamedTypeNode( loc, "Any" ), TXTC_FUNCTION, { } ), false, true );

    // create the tuple base type:
    this->builtinTypes[TXBT_TUPLE] = new TxTypeDeclNode( loc, TXD_PUBLIC | TXD_BUILTIN | TXD_ABSTRACT, "Tuple", nullptr,
                                                         new TxTupleTypeDefNode( loc, new TxNamedTypeNode( loc, "Any" ), { } ), false, true );

    // create the reference base type:
    {
        auto paramNodes = new std::vector<TxDeclarationNode*>(
                {
                  new TxTypeDeclNode( loc, TXD_PUBLIC | TXD_GENPARAM, "T", nullptr, new TxNamedTypeNode( loc, "Any" ) )
                } );
        this->builtinTypes[TXBT_REFERENCE] = new TxTypeDeclNode( loc, TXD_PUBLIC | TXD_BUILTIN, "Ref", paramNodes,
                                                                 new TxRefTypeDefNode( loc, new TxNamedTypeNode( loc, "Any" ), { } ), false, true );
    }

    // create the array base type:
    {
        auto arrayConstructors = make_array_constructors( loc );

        auto lTypeNode = new TxNamedTypeNode( loc, "UInt" );
        auto paramNodes = new std::vector<TxDeclarationNode*>(
                {
                  new TxTypeDeclNode( loc, TXD_PUBLIC | TXD_GENPARAM, "E", nullptr, new TxNamedTypeNode( loc, "Any" ) ),
                  new TxFieldDeclNode( loc, TXD_PUBLIC | TXD_GENPARAM, new TxFieldDefNode( loc, "C", lTypeNode, nullptr, false ) ),
                  //new TxFieldDeclNode( loc, TXD_PUBLIC, new TxFieldDefNode( loc, "L", lTypeNode, nullptr, false ) )
                } );
        this->builtinTypes[TXBT_ARRAY] = new TxTypeDeclNode(
                loc, TXD_PUBLIC | TXD_BUILTIN, "Array", paramNodes,
                new TxArrayTypeDefNode( loc, new TxNamedTypeNode( loc, "Any" ), arrayConstructors ), false, true );
    }

    // create the interface base type:
    {
        // the adaptee type id virtual field member, which is abstract here but concrete in adapter subtypes:
        const TxDeclarationFlags adapteeIdFieldFlags = TXD_PUBLIC | TXD_BUILTIN | TXD_STATIC | TXD_ABSTRACT | TXD_IMPLICIT;
        auto adapteeIdFType = new TxNamedTypeNode( loc, "UInt" );
        auto adapteeIdField = new TxFieldDefNode( loc, "$adTypeId", adapteeIdFType, nullptr );
        auto adapteeIdFDecl = new TxFieldDeclNode( loc, adapteeIdFieldFlags, adapteeIdField );

        auto ifTypeDef = new TxInterfaceTypeDefNode( loc, new TxNamedTypeNode( loc, "Any" ), { adapteeIdFDecl } );
        this->builtinTypes[TXBT_INTERFACE] = new TxTypeDeclNode( loc, TXD_PUBLIC | TXD_BUILTIN | TXD_ABSTRACT, "Interface", nullptr,
                                                                 ifTypeDef, true, true );
    }

    // put it all together as the AST for the tx module:

    std::vector<TxImportNode*>* imports = nullptr;
    std::vector<TxModuleNode*>* subModules = new std::vector<TxModuleNode*>();
    std::vector<TxDeclarationNode*>* members = new std::vector<TxDeclarationNode*>();

    for ( unsigned id = 0; id < BuiltinTypeId_COUNT; id++ ) {
        ASSERT( this->builtinTypes[id], "not yet coded builtin type id: " << id );
        members->push_back( this->builtinTypes[id] );
    }

    subModules->push_back( new TxModuleNode( this->builtinLocation, new TxIdentifier( "c" ),
                                             nullptr,
                                             nullptr, nullptr, true ) );

    auto module = new TxModuleNode( this->builtinLocation, new TxIdentifier( BUILTIN_NS ),
                                    imports,
                                    members, subModules, true );
    auto parsingUnit = new TxParsingUnitNode( this->builtinLocation, module );
    return parsingUnit;
}

/** Initializes the built-in symbols. */
void BuiltinTypes::initializeBuiltinSymbols() {

    // TODO: remove when initializeBuiltinSymbols() no longer creates entities needing these to be pre-resolved:
    for ( unsigned id = 0; id < BuiltinTypeId_COUNT; id++ ) {
        // verify that all built-in types are initialized:
        ASSERT( this->builtinTypes[id], "Uninitialized built-in type! id=" << id );
        this->builtinTypes[id]->typeExpression->resolve_type();
    }

    declare_tx_functions();
}

void BuiltinTypes::declare_tx_functions() {

//    auto txCfuncModule = this->registry.get_package().declare_module( this->registry.get_package().root_origin(), TxIdentifier(BUILTIN_NS ".c"), true);
    auto txCfuncModule = this->registry.package().lookup_module( "tx.c" );
    LexicalContext ctx( txCfuncModule );
    {   // declare tx.c.puts:
        auto refTypeNode = new TxReferenceTypeNode( this->builtinLocation, nullptr, new TxNamedTypeNode( this->builtinLocation, "tx.UByte" ) );
        auto argNode = new TxArgTypeDefNode( this->builtinLocation, "str", refTypeNode );
        auto c_puts_func_type_def = new TxFunctionTypeNode( this->builtinLocation, false, new std::vector<TxArgTypeDefNode*>( { argNode } ),
                                                            new TxNamedTypeNode( this->builtinLocation, "tx.Int" ) );
        auto c_puts_func_type_decl = new TxTypeDeclNode( this->builtinLocation, TXD_PUBLIC | TXD_IMPLICIT, "puts$func", nullptr,
                                                         c_puts_func_type_def );
        run_declaration_pass( c_puts_func_type_decl, ctx );
        c_puts_func_type_decl->symbol_resolution_pass();

        auto c_puts_def = new TxBuiltinFieldDefNode( this->builtinLocation );
        auto c_puts_decl = txCfuncModule->declare_field( "puts", c_puts_def, TXD_PUBLIC, TXS_GLOBAL, TxIdentifier( "" ) );
        c_puts_def->set_field( TxField::make_field( c_puts_decl, c_puts_func_type_def->resolve_type() ) );
    }
    {  // declare tx.c.abort:
//        std::vector<const TxActualType*> argumentTypes( {} );
//        auto funcType = new TxFunctionType( nullptr, this->builtinTypes[FUNCTION]->typeExpression->resolve_type()->type(), argumentTypes,
//                                            this->builtinTypes[VOID]->typeExpression->resolve_type()->type() );
//        auto type = new TxType( funcType );
        auto c_abort_func_type_def = new TxFunctionTypeNode( this->builtinLocation, false, new std::vector<TxArgTypeDefNode*>( { } ), nullptr );
        auto c_abort_func_type_decl = new TxTypeDeclNode( this->builtinLocation, TXD_PUBLIC | TXD_IMPLICIT, "abort$func", nullptr,
                                                          c_abort_func_type_def );
        run_declaration_pass( c_abort_func_type_decl, ctx );
        c_abort_func_type_decl->symbol_resolution_pass();

        auto c_abort_def = new TxBuiltinFieldDefNode( this->builtinLocation );
        auto c_abort_decl = txCfuncModule->declare_field( "abort", c_abort_def, TXD_PUBLIC, TXS_GLOBAL, TxIdentifier( "" ) );
        c_abort_def->set_field( TxField::make_field( c_abort_decl, c_abort_func_type_def->resolve_type() ) );
    }

//    auto module = this->registry.get_package().lookup_module("tx");
//    LexicalContext moduleCtx( module );

    // public _address( r : Ref ) ULong
// TODO
//    std::vector<const TxType*> argumentTypes( { this->builtinTypes[REFERENCE]->typeExpression->get_type() } );
//    auto funcTypeDef = new TxBuiltinFieldDefNode( this->builtinLocation );
//    auto funcDecl = module->declare_field("_address", funcTypeDef, TXD_PUBLIC | TXD_BUILTIN, TXS_GLOBAL, TxIdentifier(""));
//    auto type = new TxFunctionType(nullptr, this->builtinTypes[FUNCTION]->typeExpression->get_type(), argumentTypes,
//                                   this->builtinTypes[ULONG]->typeExpression->get_type());
//    funcTypeDef->set_field( TxField::make_field(funcDecl, type) );
}

BuiltinTypes::BuiltinTypes( TypeRegistry& registry )
        : registry( registry ), builtinLocation( registry.package().root_origin().get_parse_location() ) {
}

const TxType* BuiltinTypes::get_builtin_type( const BuiltinTypeId id ) const {
    return this->builtinTypes[id]->typeExpression->get_type();
}


/*=== root package definer ===*/

/** Represents the definition of the package, i.e. the root module. */
class TxPackageDefinerNode : public TxNode {
protected:
    virtual void declaration_pass() override {
        this->lexContext._scope = new TxPackage( this->get_parser_context()->driver(), *this );
    }

public:
    TxPackageDefinerNode( const TxLocation& parseLocation )
            : TxNode( parseLocation ) {
    }

    virtual TxNode* make_ast_copy() const override {
        ASSERT( false, "Can't make AST copy of TxPackageDefinerNode " << this );
        return nullptr;
    }

    TxPackage* get_package() const {
        return static_cast<TxPackage*>( this->context().scope() );
    }

    /** This node has a special declaration pass implementation since it has neither parent node or parent scope. */
    void node_declaration_pass() {
        this->declaration_pass();
    }

    virtual void symbol_resolution_pass() override {
    }

//    virtual llvm::Value* code_gen( LlvmGenerationContext& context, GenScope* scope ) const override {
//        return nullptr;
//    }

    virtual void visit_descendants( AstVisitor visitor, const AstCursor& thisCursor, const std::string& role, void* context ) override {
    }
};

TxPackage* make_root_package( TxParserContext* parserContext ) {
    auto packageDefiner = new TxPackageDefinerNode( TxLocation( nullptr, 0, 0, parserContext ) );
    packageDefiner->node_declaration_pass();
    return packageDefiner->get_package();
}
