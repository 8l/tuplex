#pragma once

#include <vector>

#include "parser/location.hpp"
#include "tx_lang_defs.hpp"
#include "type.hpp"

class TxPackage;
class TxModule;
class TxBuiltinTypeDeclNode;
class TxTypeExpressionNode;
class TxTypeDeclNode;
class TxTypeArgumentNode;
class TxTypeTypeArgumentNode;
class TxValueTypeArgumentNode;

extern std::string encode_type_name( const TxTypeDeclaration* typeDecl );

class TypeRegistry {
    static Logger& _LOG;

    TxPackage& _package;

//    /** parse location used for built-in constructs without actual source code */
    const TxLocation& get_builtin_location() const;

    /** all the type usages */
    std::vector<TxType*> usedTypes;

    std::vector<TxTypeDeclNode*> enqueuedSpecializations;

    /** set to true when the type preparation phase starts */
    bool startedPreparingTypes = false;

    /** all the types created */
    std::vector<TxActualType*> createdTypes;

    /** All the runtime types. This is all types that have meaning in runtime, and hence have a runtime type id.
     * The runtime types are a subset of all the types created during compilation. */
    std::vector<const TxActualType*> runtimeTypes;

    /** Number of built-in types.
     * The built-in types are a subset of the data types. */
    uint32_t builtinTypesCount = 0;

    /** Number of data types, i.e. all the distinct types which are concretely used (instances and/or statically) in runtime.
     * The data types are a subset of the vtable types. */
    uint32_t dataTypesCount = 0;

    /** Number of vtable types, i.e. all the distinct (non-equivalent) types with a vtable type.
     * Note, abstract types will have a vtable type id although they will not have an actual vtable entry in runtime.
     * The vtable types are a subset of the runtime types. */
    uint32_t vtableTypesCount = 0;

    uint32_t refTypesLimit = 0;

    uint32_t funcTypesLimit = 0;

    /** for the convenience method get_string_type() */
    TxTypeExpressionNode* stringTypeNode = nullptr;

    void add_type_usage( TxType* type );

    void add_type( TxActualType* type );

    // these access make_type_entity() / make_actual_type():  TODO: refactor this
    friend class TxAdapterTypeNode;
    friend class TxBuiltinTypeDefiningNode;
    friend class TxDefConstructorTypeDefNode;
    friend class TxConvConstructorTypeDefNode;
    friend class TxArrayConstructorTypeDefNode;
    friend class TxArrayEmptyConstructorTypeDefNode;
    friend class BuiltinTypes;

    /** Makes a new type entity and registers it with this registry. */
    TxType* make_type_entity( const TxActualType* actualType );

//    /** Makes a new modifiable usage of an actual type. */
//    TxActualType* make_modifiable_type( const TxTypeDeclaration* declaration, const TxActualType* baseType );

    /** Makes a new actual type and registers it with this registry. Used to make all types except original built-ins.
     * The type will be mutable if mutableType arg is true AND the base type is mutable. (Validity check is done elsewhere.) */
    TxActualType* make_actual_type( const TxTypeDeclaration* declaration, const TxActualType* baseType, bool mutableType,
                                    const std::vector<const TxType*>& interfaces = { } );

//    /** Gets a concrete "adapter type" that specializes the interface type and redirects to adaptedType. */
//    const TxInterfaceAdapterType* inner_get_interface_adapter(const TxType* interfaceType, const TxType* adaptedType);

    const TxActualType* make_actual_empty_derivation( const TxTypeDeclaration* declaration, const TxActualType* baseType, bool mutableType );

    const TxActualType* make_actual_type_derivation( const TxTypeExpressionNode* definer, const TxActualType* baseType,
                                                     const std::vector<const TxType*>& interfaces, bool mutableType );

    const TxActualType* get_actual_type_specialization( const TxTypeDefiningNode* definer, const TxActualType* baseType,
                                                        const std::vector<const TxTypeArgumentNode*>* bindings, bool mutableType );

    const TxActualType* get_inner_type_specialization( const TxTypeDefiningNode* definer, const TxActualType* baseType,
                                                       const std::vector<const TxTypeArgumentNode*>* bindings, bool mutableType );

    const TxActualType* make_type_specialization( const TxTypeDefiningNode* definer, const TxActualType* baseType,
                                                  const std::vector<const TxTypeArgumentNode*>* bindings,
                                                  ExpectedErrorClause* expErrCtx, const std::string& newBaseTypeNameStr, bool mutableType );

    const TxType* get_actual_interface_adapter( const TxActualType* interfaceType, const TxActualType* adaptedType );

    void resolve_deferred_types();

//    /** to be invoked after the resolution pass has been run on package's source, and before type preparation */
//    void enqueued_resolution_pass();

    /** to be invoked after the whole package's source has been processed, before code generation.
     * This will perform some type validation, perform data layout, and assign runtime type ids. */
    void prepare_types();

    friend TxType;

public:
    TypeRegistry( TxPackage& package );

    inline Logger* LOGGER() const {
        return &this->_LOG;
    }

    TxPackage& package() const {
        return this->_package;
    }

    /** to be invoked after the resolution pass has been run on package's source, and before type registration */
    void deferred_type_resolution_pass();

    /** Gets the enqueued specialization ASTs (e.g. for running code generation on them) */
    const std::vector<TxTypeDeclNode*>& get_enqueued_specializations() const {
        return this->enqueuedSpecializations;
    }

    void dump_types() const;

    /** Gets a built-in (primitive) type. */
    const TxType* get_builtin_type( const BuiltinTypeId id );

    /** special convenience method for the String type (which is not a primitive type). */
    const TxType* get_string_type();


    /** Returns a read-only iterator that points to the first runtime type. */
    inline std::vector<const TxActualType*>::const_iterator runtime_types_cbegin() const {
        return this->runtimeTypes.cbegin();
    }

    /** Returns a read-only iterator that points to one past the last runtime type.
     * Note, the type set order is: built-in types < data types < vtable types < runtime types */
    inline std::vector<const TxActualType*>::const_iterator runtime_types_cend() const {
        return this->runtimeTypes.cend();
    }

    /** Returns a read-only iterator that points to one past the last data type.
     * Note, the type set order is: built-in types < data types < vtable types < runtime types */
    inline std::vector<const TxActualType*>::const_iterator vtable_types_cend() const {
        return this->runtimeTypes.cbegin() + this->vtableTypesCount;
    }

    /** Returns a read-only iterator that points to one past the last data type.
     * Note, the type set order is: built-in types < data types < vtable types < runtime types */
    inline std::vector<const TxActualType*>::const_iterator data_types_cend() const {
        return this->runtimeTypes.cbegin() + this->dataTypesCount;
    }

    /** Returns a read-only iterator that points to one past the last data type.
     * Note, the type set order is: built-in types < data types < vtable types < runtime types */
    inline std::vector<const TxActualType*>::const_iterator builtin_types_cend() const {
        return this->runtimeTypes.cbegin() + this->builtinTypesCount;
    }

    /** Returns the number of runtime types.
     * Note, the type set order is: built-in types < data types < vtable types < runtime types */
    inline uint32_t runtime_types_count() const {
        return this->runtimeTypes.size();
    }

    /** Returns the Function type id limit (one past the top function type id).
     * Note, greater than reference type id limit and less than runtime type count. */
    inline uint32_t func_types_limit() const {
        return this->funcTypesLimit;
    }

    /** Returns the Ref type id limit (one past the top reference type id).
     * Note, greater than vtable types count and less than function type id limit. */
    inline uint32_t ref_types_limit() const {
        return this->refTypesLimit;
    }

    /** Returns the number of vtable types.
     * Note, the type set order is: built-in types < data types < vtable types < runtime types */
    inline uint32_t vtable_types_count() const {
        return this->vtableTypesCount;
    }

    /** Returns the number of data types.
     * Note, the type set order is: built-in types < data types < vtable types < runtime types */
    inline uint32_t data_types_count() const {
        return this->dataTypesCount;
    }

    /** Returns the number of built-in types.
     * Note, the type set order is: built-in types < data types < vtable types < runtime types */
    inline uint32_t builtin_types_count() const {
        return this->builtinTypesCount;
    }

    /*--- retrievers / creators for derived types ---*/

    /** Makes a new, empty derivation of a base type, with a distinct name. */
    const TxType* make_empty_derivation( const TxTypeDeclaration* declaration, const TxType* baseType, bool mutableType );

    /** Makes a new derivation that extends a base type and a set of interfaces.
     * The definer must have a declaration for this new type. */
    const TxType* make_type_derivation( TxTypeExpressionNode* definer, const TxType* baseType,
                                        const std::vector<const TxType*>& interfaces, bool mutableType );

//    /** Gets a modifiable 'usage' of a base type. */
//    const TxType* get_modifiable_type( const TxTypeDeclaration* declaration, const TxType* type );

    /** Gets/makes a specialization of a generic base type.
     * If such a specialization already exists, that will be returned. */
    const TxType* get_type_specialization( TxTypeDefiningNode* definer, const TxType* baseType,
                                           const std::vector<const TxTypeArgumentNode*>& bindings, bool mutableType );

    /** Gets a concrete "adapter type" that specializes the interface type and redirects to adaptedType.
     * If such an adapter already exists, that will be returned. */
    const TxType* get_interface_adapter( const TxType* interfaceType, const TxType* adaptedType );

    const TxType* get_reference_type( TxTypeDefiningNode* definer, const TxTypeTypeArgumentNode* targetTypeBinding,
                                      const TxIdentifier* dataspace );

    const TxType* get_array_type( TxTypeDefiningNode* definer, const TxTypeTypeArgumentNode* elemTypeBinding,
                                  const TxValueTypeArgumentNode* capacityBinding, bool mutableType=false );
    const TxType* get_array_type( TxTypeDefiningNode* definer, const TxTypeTypeArgumentNode* elemTypeBinding, bool mutableType=false );

    // "mod" of function refers to whether functions of this type may modify its closure when run.
    // Note: "mod" of args not part of the function type (though concrete function may mod-ify its stack arg copies).
    const TxType* get_function_type( const TxTypeDeclaration* declaration, const std::vector<const TxType*>& argumentTypes,
                                     const TxType* returnType, bool modifying );

    const TxType* get_function_type( const TxTypeDeclaration* declaration, const std::vector<const TxType*>& argumentTypes,
                                     bool modifying );

    const TxType* get_externc_function_type( const TxTypeDeclaration* declaration, const std::vector<const TxType*>& argumentTypes,
                                             const TxType* returnType );

    const TxType* get_constructor_type( const TxTypeDeclaration* declaration, const std::vector<const TxType*>& argumentTypes,
                                        const TxTypeDeclaration* constructedObjTypeDecl );
};
