#include "type.hpp"
#include "ast.hpp"
#include "entity.hpp"


//std::string TxTypeUsage::to_string() const {
//    return modifiable ? ("~" + type_proxy->get_type()->to_string()) : type_proxy->get_type()->to_string();
//}


bool TxTypeBinding::operator==(const TxTypeBinding& other) const {
    return ( this->typeParamName == other.typeParamName
             && this->metaType == other.metaType
             && ( this->is_redeclared()
                  ? ( other.is_redeclared() && this->redeclParam == other.redeclParam )
                  : ( !other.is_redeclared()
                      && ( this->metaType == TxTypeParam::MetaType::TXB_TYPE
                           ? ( *(this->typeProxy->get_type()) == *(other.typeProxy->get_type()) )
                           : ( this->valueProxy->get_int_value() == other.valueProxy->get_int_value() ) ) ) ) );
}

std::string TxTypeBinding::to_string() const {
    const TxType* type;
    return this->typeParamName + "=" + ( this->is_redeclared() ? this->redeclParam.param_name()
                                            : ( this->metaType==TxTypeParam::MetaType::TXB_TYPE
                                                ? (type = this->typeProxy->get_type(),
                                                   type ? type->to_string(true) : "")
                                                : std::to_string(this->valueProxy->get_int_value()) ) );
                                                //: "valueproxy" ) );
}


bool TxTypeSpecialization::operator==(const TxTypeSpecialization& other) const {
    return ( this->type == other.type
             || ( this->type && other.type && *this->type == *other.type) )
           && this->modifiable == other.modifiable
           && this->bindings == other.bindings;
}

std::string TxTypeSpecialization::to_string() const { return "specialization of " + this->type->to_string(); }

std::string TxTypeSpecialization::validate() const {
    if (this->type->is_modifiable())
        return std::string("Can't specialize a 'modifiable' type (specialize its base type instead).");
    if (this->modifiable) {
        if (this->type->is_immutable())
            return std::string("Can't make an immutable type modifiable.");
        if (! this->bindings.empty())
            return std::string("Can't bind type parameters on top of a 'modifiable' type.");
    }
    else if (! this->bindings.empty()) {
        // if at least one param is bound, all need to be bound/redeclared
        if (this->type->type_params().size() != this->bindings.size())
            return std::string("All parameters must have binding (which may in turn be an unbound parameter of derived type)");
        for (auto & b : this->bindings) {
            if (this->type->has_type_param(b.param_name())) {
                // validate metatype and constraints
                auto p = this->type->get_type_param(b.param_name());
                if (b.meta_type() != p.meta_type())
                    return std::string("Binding for type parameter ") + p.to_string() + " of wrong meta-type (TYPE vs VALUE)";
                if (b.is_redeclared()) {
                    // TODO: check that redeclaration fulfills original parameter's constraints
                }
                else if (b.meta_type() == TxTypeParam::MetaType::TXB_VALUE) {
                    // TODO: check: VALUE parameters can not be of modifiable type
                }
                else {
                    if (p.has_base_type_definer()) {
                        auto constraintType = p.get_base_type_definer()->get_type();
                        auto boundType = b.type_proxy().get_type();
                        if (boundType && ! boundType->is_a(*constraintType))
                            return std::string("Binding for type parameter ") + p.to_string() + " is not a derivation of type " + constraintType->to_string();
                    }
                }
            }
            else
                return std::string("No type parameter matches provided binding " + b.to_string());
        }
    }
    return std::string();
}



bool TxType::is_pure_specialization() const {
    return (this->baseTypeSpec.type && this->interfaces.empty()
            && typeid(*this) == typeid(*this->baseTypeSpec.type)
            && !(this->_entity && this->_entity->has_instance_fields()));
}

bool TxType::is_empty_specialization() const {
    return (this->baseTypeSpec.type && this->interfaces.empty()
            && typeid(*this) == typeid(*this->baseTypeSpec.type)
            && this->baseTypeSpec.bindings.empty()
            && !this->baseTypeSpec.modifiable
            && !(this->_entity && this->_entity->has_instance_fields()));
}

bool TxType::is_virtual_specialization() const {
    return ( this->baseTypeSpec.type
             && typeid(*this) == typeid(*this->baseTypeSpec.type)
             && ( this->baseTypeSpec.bindings.empty()
                  || std::all_of( this->baseTypeSpec.bindings.cbegin(), this->baseTypeSpec.bindings.cend(),
                                  [](const TxTypeBinding& b) { return b.is_redeclared(); } ) )
             && !( this->_entity && this->_entity->has_instance_fields() ) );
}


const TxSymbolScope* TxType::lookup_instance_member(std::vector<const TxSymbolScope*>& path, const TxIdentifier& ident) const {
    if (this->_entity)
        return this->_entity->lookup_instance_member(path, ident);
        // (the entity will in turn call this type's lookup_inherited_member() if it needs to, so don't call it from here)
    else
        return this->lookup_inherited_instance_member(path, ident);
}

const TxSymbolScope* TxType::lookup_inherited_instance_member(std::vector<const TxSymbolScope*>& path, const TxIdentifier& ident) const {
    if (this->baseTypeSpec.type)
        return this->baseTypeSpec.type->lookup_instance_member(path, ident);
    // FUTURE: implemented interfaces
    return nullptr;
}

const TxSymbolScope* TxType::lookup_member(std::vector<const TxSymbolScope*>& path, const TxIdentifier& ident) const {
    if (this->_entity)
        return this->_entity->lookup_member(path, ident);
        // (the entity will in turn call this type's lookup_inherited_member() if it needs to, so don't call it from here)
    else
        return this->lookup_inherited_member(path, ident);
}

const TxSymbolScope* TxType::lookup_inherited_member(std::vector<const TxSymbolScope*>& path, const TxIdentifier& ident) const {
    if (this->baseTypeSpec.type)
        return this->baseTypeSpec.type->lookup_member(path, ident);
    // FUTURE: implemented interfaces
    return nullptr;
}


static void type_bindings_string(std::stringstream& str, const TxTypeSpecialization& specialization) {
    str << "<";
    int ix = 0;
    for (auto & b : specialization.bindings) {
        if (ix++)  str << ",";
        str << b;
    }
    str << ">";
}

void TxType::self_string(std::stringstream& str, bool brief) const {
    bool fold = true;
    if (this->is_modifiable()) {
        str << "MOD ";
        //fold = false;
    }
    auto entity = this->entity();
    if (brief && entity) {
        str << entity->get_full_name();
        if (this->is_generic())
            str << this->type_params_string();
//            if (this->baseTypeSpec.type) {
//                str << " : ";
//                this->baseTypeSpec.type->self_string(str);
//            }
    }
    else if (this->baseTypeSpec.type) {
        //if (!this->is_pure_specialization() || this->is_generic() || typeid(*this) != typeid(*baseTypeSpec.type)) {
            str << typeid(*this).name();
            if (this->is_generic())
                str << this->type_params_string();
            fold = false;
        //}
        if (! this->baseTypeSpec.bindings.empty()) {
            type_bindings_string(str, this->baseTypeSpec);
            fold = false;
        }
        if (! fold)
            str << " : ";
        this->baseTypeSpec.type->self_string(str, true);
    }
    else {
        str << typeid(*this).name();
    }
}



bool TxTupleType::operator==(const TxType& other) const {
    // FUTURE: allow polymorphic compatibility
    return (typeid(*this) == typeid(other) &&
            *this->entity() == *((TxTupleType&)other).entity());
}



//TxTypeProxy* make_identified_type_proxy(const std::string& name) {
//    auto identNode = new TxEntityIdentNode(YYLTYPE(), TxIdentifierNode::TYPE_ID, new TxIdentifier(name));
//    auto typeNode = new TxIdentifiedTypeNode(YYLTYPE(), *identNode);
//    return typeNode;
//}
//
//TxConstantProxy* make_identified_field_proxy(const std::string& name) {
//    auto identNode = new TxEntityIdentNode(YYLTYPE(), TxIdentifierNode::FIELD_ID, new TxIdentifier(name));
//    auto fieldNode = new TxIdentifiedFieldNode(YYLTYPE(), *identNode);
//    return fieldNode;
//}
