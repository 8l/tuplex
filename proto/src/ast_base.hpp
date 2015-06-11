#pragma once

#include <typeinfo>
#include <vector>

#include "assert.hpp"
#include "logging.hpp"
#include "tx_error.hpp"

#include "tx_operations.hpp"
#include "identifier.hpp"
#include "package.hpp"
#include "module.hpp"
#include "entity.hpp"

#include "driver.hpp"
#include "location.hh"


/* forward declarations pertaining to LLVM code generation */
class LlvmGenerationContext;
class GenScope;
namespace llvm {
    class Value;
}


/** Represents a value that can be statically computed (in compile time). */
class TxConstantProxy : public TxTypeProxy {
public:
    virtual ~TxConstantProxy() = default;

    /** Gets the TxType instance representing the type of the constant. */
    virtual const TxType* get_type() const override = 0;

    virtual uint32_t get_value_UInt() const = 0;

    virtual llvm::Constant* code_gen(LlvmGenerationContext& context, GenScope* scope) const = 0;

    virtual bool operator==(const TxConstantProxy& other) const;

    inline virtual bool operator!=(const TxConstantProxy& other) const final {
        return ! this->operator==(other);
    }
};



class TxNode : public virtual TxParseOrigin, public Printable {
    static Logger& LOG;

    LexicalContext lexContext;

protected:
    void set_context(const LexicalContext& context) {
        ASSERT(!this->is_context_set(), "lexicalContext already initialized in " << this->to_string());
        this->lexContext = context;
    }
    /** Sets the lexical context of this node to the current context of the module. */
    void set_context(TxScopeSymbol* lexContext) {
        this->set_context(LexicalContext(lexContext));
    }

    inline TypeRegistry& types() { return this->context().package()->types(); }

public:
    const yy::location parseLocation;

    TxNode(const yy::location& parseLocation) : lexContext(), parseLocation(parseLocation) { }

    virtual ~TxNode() {
        if (this->is_context_set())
            LOGGER().debug("Running destructor of %s", this->to_string().c_str());
    }

    inline bool is_context_set() const { return this->lexContext.scope(); }

    /** Sets the lexical context of this node to be equal to that of the provided node. */
    void set_context(const TxNode* node) {
        this->set_context(node->context());
    }

    inline const LexicalContext& context() const {
        ASSERT(this->is_context_set(), "lexicalContext not initialized in " << this->to_string());
        return this->lexContext;
    }
    inline LexicalContext& context() {
        return const_cast<LexicalContext&>(static_cast<const TxNode *>(this)->context());
    }

    virtual TxDriver* get_driver() const override {
        return ( this->is_context_set() ? &this->context().package()->driver() : nullptr );
    }

    virtual const yy::location& get_parse_location() const override {
        return this->parseLocation;
    }


    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const = 0;

    virtual std::string to_string() const;

    std::string parse_loc_string() const;

    inline Logger& LOGGER() const { return this->LOG; }
};


bool validateTypeName(TxNode* node, TxDeclarationFlags declFlags, const std::string& name);
bool validateFieldName(TxNode* node, TxDeclarationFlags declFlags, const std::string& name);


class TxIdentifierNode : public TxNode {
public:
    enum IdentifierClass { UNSPECIFIED, MODULE_ID, IMPORT_ID, TYPE_ID, FIELD_ID, DATASPACE_ID };
    const IdentifierClass idClass;
    const TxIdentifier ident;

    TxIdentifierNode(const yy::location& parseLocation, const TxIdentifier* ident, IdentifierClass identifierClass=UNSPECIFIED)
        : TxNode(parseLocation), idClass(identifierClass), ident(*ident)  { }

    TxIdentifierNode(const yy::location& parseLocation, const TxIdentifier& ident)
        : TxNode(parseLocation), idClass(UNSPECIFIED), ident(ident)  { }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override { return nullptr; }

    virtual std::string to_string() const {
        return TxNode::to_string() + " '" + this->ident.to_string() + "'";
    }
};



class TxImportNode : public TxNode {
public:
    const TxIdentifierNode* identNode;

    TxImportNode(const yy::location& parseLocation, const TxIdentifierNode* identifier)
        : TxNode(parseLocation), identNode(identifier)  { }

    virtual void symbol_declaration_pass(TxModule* module) {
        this->set_context(module);
        if (! identNode->ident.is_qualified())
            CERROR(this, "can't import unqualified identifier '" << identNode->ident << "'");
        module->register_import(identNode->ident);
    }

    virtual void symbol_resolution_pass(ResolutionContext& resCtx) {
    }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override { return nullptr; }
};


class TxDeclarationNode : public TxNode {  // either type or field
public:
    const TxDeclarationFlags declFlags;

    TxDeclarationNode(const yy::location& parseLocation, const TxDeclarationFlags declFlags)
        : TxNode(parseLocation), declFlags(declFlags) { }

    virtual void symbol_declaration_pass(LexicalContext& lexContext) = 0;

    virtual void symbol_resolution_pass(ResolutionContext& resCtx) = 0;
};


class TxModuleNode : public TxNode {
    const TxIdentifierNode* identNode;
    std::vector<TxImportNode*>* imports;
    std::vector<TxDeclarationNode*>* members;
    std::vector<TxModuleNode*>* subModules;
    TxModule* module;  // set in symbol table pass
public:
    TxModuleNode(const yy::location& parseLocation, const TxIdentifierNode* identifier,
                 std::vector<TxImportNode*>* imports, std::vector<TxDeclarationNode*>* members,
                 std::vector<TxModuleNode*>* subModules)
        : TxNode(parseLocation), identNode(identifier), imports(imports), members(members), subModules(subModules), module()  {
        ASSERT(identifier, "NULL identifier");  // (sanity check on parser)
    }

    virtual void symbol_declaration_pass(TxModule* parent) {
        this->set_context(parent);
        this->module = parent->declare_module(identNode->ident);
        if (this->imports) {
            for (auto elem : *this->imports)
                elem->symbol_declaration_pass(this->module);
        }
        if (this->members) {
            LexicalContext lexContext(this->module);
            for (auto elem : *this->members)
                elem->symbol_declaration_pass(lexContext);
        }
        if (this->subModules) {
            for (auto mod : *this->subModules)
                mod->symbol_declaration_pass(this->module);
        }
    }

    virtual void symbol_resolution_pass(ResolutionContext& resCtx) {
        if (this->imports) {
            for (auto elem : *this->imports)
                elem->symbol_resolution_pass(resCtx);
        }
        if (this->members) {
            for (auto elem : *this->members)
                elem->symbol_resolution_pass(resCtx);
        }
        if (this->subModules) {
            for (auto mod : *this->subModules)
                mod->symbol_resolution_pass(resCtx);
        }
    }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;
};


/** Represents a parsing unit, i.e. a given source text input (e.g. source file). */
class TxParsingUnitNode : public TxNode {
    std::vector<TxModuleNode*> modules;
public:

    TxParsingUnitNode(const yy::location& parseLocation) : TxNode(parseLocation) { }

    void add_module(TxModuleNode* module) {
        this->modules.push_back(module);
    }

    virtual void symbol_declaration_pass(TxPackage* package) {
        for (auto mod : this->modules)
            mod->symbol_declaration_pass(package);
    }

    virtual void symbol_resolution_pass(ResolutionContext& resCtx) {
        for (auto mod : this->modules)
            mod->symbol_resolution_pass(resCtx);
    }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;
};



class TxStatementNode : public TxNode {
public:
    TxStatementNode(const yy::location& parseLocation) : TxNode(parseLocation) { }
    virtual void symbol_declaration_pass(LexicalContext& lexContext) = 0;
    virtual void symbol_resolution_pass(ResolutionContext& resCtx) = 0;
};



/**
 * The context of this node refers to its outer scope. This node's entity, if any, refers to its inner scope.
 */
class TxTypeExpressionNode : public TxNode, public TxTypeDefiner {
    bool isResolving = false;  // during development - guard against recursive calls to get_type()
    bool hasResolved = false;  // to prevent multiple identical error messages
    TxType const * type = nullptr;
    TxTypeDeclaration* declaration = nullptr;  // null unless initialized in symbol declaration pass

protected:
    /** if parent node is a type declaration that declares type parameters, these will be set by it */
    const std::vector<TxDeclarationNode*>* typeParamDeclNodes = nullptr;
    const std::vector<TxTypeParam>* declTypeParams = nullptr;

    virtual void set_entity(TxTypeDeclaration* declaration) {
        ASSERT(!this->declaration, "declaredEntity already set in " << this);
        this->declaration = declaration;
    }

    /** Gets the type declaration of this type expression, if any. */
    virtual TxTypeDeclaration* get_declaration() const { return this->declaration; }

    virtual void symbol_declaration_pass_descendants(LexicalContext& defContext, LexicalContext& lexContext, TxDeclarationFlags declFlags) = 0;

    /** Defines the type of this type expression, constructing/obtaining the TxType instance.
     * The implementation should only traverse the minimum nodes needed to define the type
     * (e.g. not require the actual target type of a reference to be defined).
     * This should only be invoked once, from the TxTypeExpressionNode class. */
    virtual const TxType* define_type(ResolutionContext& resCtx) = 0;

public:
    TxTypeExpressionNode(const yy::location& parseLocation) : TxNode(parseLocation)  { }

    /** if parent node is a type declaration that declares type parameters, this is invoked by it */
    virtual void setTypeParams(const std::vector<TxDeclarationNode*>* typeParamDeclNodes);

    /** Returns true if this type expression is a directly identified type
     * (i.e. a previously declared type, does not construct a new type). */
    virtual bool has_predefined_type() const { return false; }


    /** Performs the symbol declaration pass for this type expression.
     * Type expressions evaluate within a "definition context", representing their "outer" scope,
     * and a "lexical context", within which they declare their constituent sub-expressions.
     * The definition context is used for named types lookups, to avoid conflation with names of the sub-expressions.
     */
    virtual void symbol_declaration_pass(LexicalContext& defContext, LexicalContext& lexContext, TxDeclarationFlags declFlags,
                                         const std::string designatedTypeName = std::string());

    virtual void symbol_resolution_pass(ResolutionContext& resCtx) {
        this->resolve_type(resCtx);
    }

    virtual const TxType* resolve_type(ResolutionContext& resCtx) override final {
        if (!type && !hasResolved) {
            LOGGER().trace("resolving type of %s", this->to_string().c_str());
            ASSERT(!isResolving, "Recursive invocation of resolve_type() of " << this);
            this->isResolving = true;
            this->type = this->define_type(resCtx);
            this->hasResolved = true;
            this->isResolving = false;
//            if (this->cachedType && this->declaredEntity)
//                ASSERT(this->cachedType->entity()==this->declaredEntity || this->declaredEntity->get_alias(),
//                        "entity " << this->cachedType->entity() << " (of type " << this->cachedType
//                        << ") is not same as declared entity " << this->declaredEntity
//                        << " (of node " << *this << ")");
        }
        return type;
    }

    virtual const TxType* attempt_get_type() const override final {
        return type;
    }
    inline virtual const TxType* get_type() const override final {
        ASSERT(this->is_context_set(), "Can't call get_type() before symbol table pass has completed: "  << this);
        ASSERT(type, "Type not set in " << this);
        return type;
    }
};

/** Wraps a TxTypeDefiner within a TxTypeExpressionNode. */
class TxTypeWrapperNode : public TxTypeExpressionNode {
    TxTypeDefiner* typeDefiner;
protected:
    virtual void symbol_declaration_pass_descendants(LexicalContext& defContext, LexicalContext& lexContext,
                                                     TxDeclarationFlags declFlags) override { }

    virtual const TxType* define_type(ResolutionContext& resCtx) override {
        return this->typeDefiner->resolve_type(resCtx);
    }

public:
    TxTypeWrapperNode(const yy::location& parseLocation, TxTypeDefiner* typeDefiner)
        : TxTypeExpressionNode(parseLocation), typeDefiner(typeDefiner)  { }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override { return nullptr; }
};


class TxFieldDefNode;

class TxExpressionNode : public TxNode, public TxTypeDefiner {
    bool isResolving = false;  // during development - guard against recursive calls to get_type()
    bool hasResolved = false;  // to prevent multiple identical error messages
    TxType const * type = nullptr;
protected:
    const TxFieldDefNode* fieldDefNode = nullptr; // injected by field definition if known and applicable
    std::vector<const TxType*>* appliedFuncArgTypes = nullptr; // injected by expression context if applicable

    /** Defines the type of this expression (as specific as can be known), constructing/obtaining the TxType instance.
     * The implementation should only traverse the minimum nodes needed to define the type
     * (e.g. not require the actual target type of a reference to be defined).
     * This should only be invoked once, from the TxExpressionNode class. */
    virtual const TxType* define_type(ResolutionContext& resCtx) = 0;

public:
    TxExpressionNode(const yy::location& parseLocation) : TxNode(parseLocation) { }

    /** Injected by field definition if known and applicable. */
    virtual void set_field_def_node(const TxFieldDefNode* fieldDefNode) {
        this->fieldDefNode = fieldDefNode;
    }

    /** Returns true if this value expression is of a directly identified type
     * (i.e. does not construct a new type), e.g. value literals and directly identified fields. */
    virtual bool has_predefined_type() const = 0;

    virtual void symbol_declaration_pass(LexicalContext& lexContext) = 0;

    virtual void symbol_resolution_pass(ResolutionContext& resCtx) {
        this->resolve_type(resCtx);
    }

    /** Returns the type (as specific as can be known) of the value this expression produces. */
    virtual const TxType* resolve_type(ResolutionContext& resCtx) override final {
        if (!this->type && !hasResolved) {
            LOGGER().trace("resolving type of %s", this->to_string().c_str());
            ASSERT(!isResolving, "Recursive invocation of resolve_type() of " << this);
            this->isResolving = true;
            this->type = this->define_type(resCtx);
            this->hasResolved = true;
            this->isResolving = false;
            //if (! this->cachedType)
            //    LOGGER().warning("%s: resolve_type() yielded NULL", this->parse_loc_string().c_str());
        }
        return this->type;
    }

    virtual const TxType* attempt_get_type() const override final {
        return this->type;
    }
    /** Returns the type (as specific as can be known) of the value this expression produces. */
    virtual const TxType* get_type() const override final {
        // (for now) not a strict requirement, these nodes are sometimes added dynamically (e.g. conversions):
        //ASSERT(this->is_context_set(), "Can't call get_type() before symbol table pass has completed: "  << this);
        return this->type;
    }

    ///** Returns true if this expression represents a specific storage location (e.g. a field). */
    //virtual bool has_address() const { return false; }

    /** Returns true if this expression is a constant expression that can be evaluated at compile time. */
    virtual bool is_statically_constant() const { return false; }

    /** If this expression can currently be statically evaluated,
     * a TxConstantProxy representing its value is returned, otherwise nullptr.
     * In future, this should return non-null for all expressions for which is_statically_constant() returns true.
     */
    virtual const TxConstantProxy* get_static_constant_proxy() const {
        //throw std::logic_error("Getting constant proxy not supported for expression node " + this->to_string());
        return nullptr;
    }

    virtual bool hasAppliedFuncArgTypes()  { return this->appliedFuncArgTypes; }
    virtual std::vector<const TxType*>* get_applied_func_arg_types()  { return this->appliedFuncArgTypes; }
    virtual void set_applied_func_arg_types(std::vector<const TxType*>* appliedFuncArgTypes) {
        this->appliedFuncArgTypes = appliedFuncArgTypes;
    }

    /** Generates code that produces the type id (as opposed to the value) of this expression. */
    virtual llvm::Value* code_gen_typeid(LlvmGenerationContext& context, GenScope* scope) const;
};


/** Checks that an expression has a type that matches the required type, and wraps
 * a value & type conversion node around it if permitted and necessary.
 *
 * Assumes that originalExpr symbol registration pass has already run.
 * Will run symbol registration and symbol resolution passes on any inserted nodes.
 * Does not run the semantic pass on inserted nodes.
 */
TxExpressionNode* validate_wrap_convert(ResolutionContext& resCtx, TxExpressionNode* originalExpr, const TxType* requiredType, bool _explicit=false);

/** Checks that an rvalue expression of an assignment or argument to a funciton call
 * has a type that matches the required type,
 * and wraps a value & type conversion node around it if permitted and necessary.
 *
 * Assumes that originalExpr symbol registration pass has already run.
 * Will run symbol registration and symbol resolution passes on any inserted nodes.
 * Does not run the semantic pass on inserted nodes.
 */
TxExpressionNode* validate_wrap_assignment(ResolutionContext& resCtx, TxExpressionNode* rValueExpr, const TxType* requiredType);


class TxFieldDefNode : public TxNode, public TxFieldDefiner {
    TxFieldDeclaration* declaration = nullptr;  // null until initialized in symbol declaration pass
    TxType const * type = nullptr;
    TxField const * field = nullptr;

    void symbol_declaration_pass(LexicalContext& outerContext, LexicalContext& innerContext, TxDeclarationFlags declFlags) {
        this->set_context(outerContext);
        auto typeDeclFlags = (declFlags & (TXD_PUBLIC | TXD_PROTECTED)) | TXD_IMPLICIT;
        if (this->typeExpression) {
            // unless the type expression is a directly named type, declare implicit type entity for this field's type:
            if (this->typeExpression->has_predefined_type())
                this->typeExpression->symbol_declaration_pass(innerContext, innerContext, typeDeclFlags);
            else {
                auto implTypeName = this->get_field_name() + "$type";
                this->typeExpression->symbol_declaration_pass(innerContext, innerContext, typeDeclFlags, implTypeName);
            }
        }
        if (this->initExpression) {
// TODO: delegate this to the expression nodes
//            if (!this->typeExpression && !this->initExpression->has_predefined_type()) {
//                // declare implicit type entity for this field's type:
//                TxTypeEntity* typeEntity = lexContext.scope()->declare_type(implTypeName, this->typeExpression, typeDeclFlags);
//                if (!typeEntity)
//                    CERROR(this, "Failed to declare implicit type %s for field %s", implTypeName.c_str(), this->fieldName.c_str());
//            }
            this->initExpression->symbol_declaration_pass(outerContext);
        }
    };

public:
    const std::string fieldName;  // the original field name
    const bool modifiable;  // true if field name explicitly declared modifiable
    TxTypeDefiner* typeDefiner;  // optional, non-code-generating type definer (can't be specified at same time as typeExpression)
    TxTypeExpressionNode* typeExpression;
    TxExpressionNode* initExpression;

    TxFieldDefNode(const yy::location& parseLocation, const std::string& fieldName,
                   TxTypeExpressionNode* typeExpression, TxExpressionNode* initExpression)
            : TxNode(parseLocation), fieldName(fieldName), modifiable(false), typeDefiner() {
        this->typeExpression = typeExpression;
        this->initExpression = initExpression;
        if (this->initExpression)
            this->initExpression->set_field_def_node(this);
    }
    TxFieldDefNode(const yy::location& parseLocation, const std::string& fieldName,
                   TxExpressionNode* initExpression, bool modifiable=false, TxTypeDefiner* typeDefiner=nullptr)
            : TxNode(parseLocation), fieldName(fieldName), modifiable(modifiable), typeDefiner(typeDefiner) {
        this->typeExpression = nullptr;
        this->initExpression = initExpression;
        if (this->initExpression)
            this->initExpression->set_field_def_node(this);
    }

    void symbol_declaration_pass_local_field(LexicalContext& lexContext, bool create_local_scope) {
        auto outerCtx = lexContext;  // prevents init expr from referring to this field
        if (create_local_scope)
            lexContext.scope(lexContext.scope()->create_code_block_scope());
        TxDeclarationFlags declFlags = TXD_NONE;
        this->declaration = lexContext.scope()->declare_field(this->fieldName, this, declFlags, TXS_STACK, TxIdentifier(""));
        this->symbol_declaration_pass(outerCtx, lexContext, declFlags);
    }

    void symbol_declaration_pass_nonlocal_field(LexicalContext& lexContext, TxDeclarationFlags declFlags,
                                                 TxFieldStorage storage, const TxIdentifier& dataspace) {
        std::string declName;
        if (this->fieldName != "self")
            declName = this->fieldName;
        else {
            // handle constructor declaration
            declName = "$init";
            declFlags = declFlags | TXD_CONSTRUCTOR;
        }

        this->declaration = lexContext.scope()->declare_field(declName, this, declFlags, storage, dataspace);
        this->symbol_declaration_pass(lexContext, lexContext, declFlags);
    }

    void symbol_declaration_pass_functype_arg(LexicalContext& lexContext) {
        this->symbol_declaration_pass(lexContext, lexContext, TXD_NONE);
    }

    virtual void symbol_resolution_pass(ResolutionContext& resCtx) {
        this->resolve_field(resCtx);
        if (this->typeExpression) {
            this->typeExpression->symbol_resolution_pass(resCtx);
        }
        if (this->initExpression) {
            this->initExpression->symbol_resolution_pass(resCtx);
            if ((this->typeExpression || this->typeDefiner) && this->type)
                this->initExpression = validate_wrap_convert(resCtx, this->initExpression, this->type);
            if (this->field && this->field->is_statically_constant())
                    if (! this->initExpression->is_statically_constant())
                        CERROR(this, "Non-constant initializer for constant global/static field.");
        }

        if (auto type = this->get_type()) {
            if (! type->is_concrete())
                CERROR(this, "Field type is not concrete (size potentially unknown): " << type);
            if (this->get_field_name() == "$init") {
                if (this->get_declaration()->get_storage() != TXS_INSTANCEMETHOD)
                    CERROR(this, "Illegal declaration name for non-constructor member: " << this->fieldName);
                // TODO: check that constructor function type has void return value
            }
        }
    }

    virtual const TxField* resolve_field(ResolutionContext& resCtx) override {
        if (! this->field) {
            LOGGER().trace("resolving type of %s", this->to_string().c_str());
            if (this->typeExpression) {
                this->type = this->typeExpression->resolve_type(resCtx);
            }
            else if (this->typeDefiner) {
                this->type = this->typeDefiner->resolve_type(resCtx);
            }
            else {
                this->type = this->initExpression->resolve_type(resCtx);
                if (this->type) {
                    if (this->modifiable) {
                        if (! this->type->is_modifiable())
                            this->type = this->types().get_modifiable_type(nullptr, this->type);
                    }
                    else if (this->type->is_modifiable())
                        // if initialization expression is modifiable type, and modifiable not explicitly specified,
                        // lose modifiable attribute (modifiability must be explicit)
                        this->type = this->type->get_base_type();
                }
            }

            if (this->type && this->declaration)
                this->field = new TxField(this->declaration, this->type);
        }
        return this->field;
    }

    virtual const TxField* get_field() const override {
        ASSERT(this->field, "NULL field in " << *this);  // note, is null in case of function type arg
        return this->field;
    }

    virtual const TxType* resolve_type(ResolutionContext& resCtx) override {
        resolve_field(resCtx);
        return this->type;
    }

    virtual const TxType* attempt_get_type() const override {
        return this->type;
    }
    virtual const TxType* get_type() const override {
        return this->type;
    }

    virtual const TxExpressionNode* get_init_expression() const override {
        return this->initExpression;
    }

    /** Gets the plain name of this field as defined in the source text. */
    inline const std::string& get_field_name() const {
        return this->fieldName;
    }

    TxFieldDeclaration* get_declaration() const {
        ASSERT(this->declaration, "field declaration not initialized for " << this->fieldName);
        return this->declaration;
    }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;

    virtual std::string to_string() const override {
        return TxNode::to_string() + " '" + this->get_field_name() + "'";
    }
};

/** Non-local field declaration */
class TxFieldDeclNode : public TxDeclarationNode {
    const bool isMethodSyntax = false;
public:
    TxFieldDefNode* field;

    TxFieldDeclNode(const yy::location& parseLocation, const TxDeclarationFlags declFlags, TxFieldDefNode* field,
                    bool isMethodSyntax=false)
            : TxDeclarationNode(parseLocation, declFlags), isMethodSyntax(isMethodSyntax), field(field) { }

    virtual void symbol_declaration_pass(LexicalContext& lexContext) override;

    virtual void symbol_resolution_pass(ResolutionContext& resCtx) override {
        this->field->symbol_resolution_pass(resCtx);

        if (auto type = this->field->get_type()) {
            auto storage = this->field->get_declaration()->get_storage();
            if (type->is_modifiable()) {
                if (storage == TXS_GLOBAL)
                    CERROR(this, "Global fields may not be modifiable: " << field->get_field_name().c_str());
            }
            else if (! this->field->initExpression) {
                if (storage == TXS_GLOBAL || storage == TXS_STATIC)
                    if (! (this->field->get_declaration()->get_decl_flags() & TXD_GENPARAM))
                        CERROR(this, "Non-modifiable field must have an initializer");
                // FUTURE: ensure TXS_INSTANCE fields are initialized either here or in every constructor
                // FUTURE: check that TXS_STACK fields are initialized before first use
            }
        }
    }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;
};


/** Non-local type declaration */
class TxTypeDeclNode : public TxDeclarationNode {
public:
    const std::string typeName;
    const std::vector<TxDeclarationNode*>* typeParamDecls;
    TxTypeExpressionNode* typeExpression;

    TxTypeDeclNode(const yy::location& parseLocation, const TxDeclarationFlags declFlags,
                   const std::string typeName, const std::vector<TxDeclarationNode*>* typeParamDecls,
                   TxTypeExpressionNode* typeExpression)
        : TxDeclarationNode(parseLocation, declFlags),
          typeName(typeName), typeParamDecls(typeParamDecls), typeExpression(typeExpression) {
        validateTypeName(this, declFlags, typeName);
        if (typeParamDecls)
            this->typeExpression->setTypeParams(typeParamDecls);
    }

    virtual void symbol_declaration_pass(LexicalContext& lexContext)  { this->symbol_declaration_pass(lexContext, lexContext); }
    virtual void symbol_declaration_pass(LexicalContext& defContext, LexicalContext& lexContext);

    virtual void symbol_resolution_pass(ResolutionContext& resCtx) {
        this->typeExpression->symbol_resolution_pass(resCtx);
        if (this->typeParamDecls)
            for (auto paramDecl : *this->typeParamDecls)
                paramDecl->symbol_resolution_pass(resCtx);
    }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;
};


class TxAssigneeNode : public TxNode {
    bool hasResolved = false;
    TxType const * type = nullptr;

    /** Defines/obtains the type (as specific as can be known) of the value of this assignee.
     * Must be defined by all TxAssigneeNode subclasses, but should only be invoked from TxAssigneeNode. */
    virtual const TxType* define_type(ResolutionContext& resCtx) = 0;

public:
    TxAssigneeNode(const yy::location& parseLocation) : TxNode(parseLocation) { }
    virtual void symbol_declaration_pass(LexicalContext& lexContext) = 0;

    virtual const TxType* resolve_type(ResolutionContext& resCtx) final {
        if (! hasResolved) {
            hasResolved = true;
            this->type = this->define_type(resCtx);
        }
        return type;
    }

    virtual void symbol_resolution_pass(ResolutionContext& resCtx) {
        this->resolve_type(resCtx);
    }

    /** Gets the type of this assignee. */
    virtual const TxType* get_type() const final {
        return this->type;
    }
};
