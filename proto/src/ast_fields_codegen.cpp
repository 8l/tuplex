#include "ast_exprs.hpp"
#include "llvm_generator.hpp"

using namespace llvm;


/** Convenience function that returns true if type is a pointer whose destination should be
 * directly loaded / stored when accessed. */
static bool access_via_load_store(const llvm::Type* type) {
    bool ret = (type->isPointerTy() && type->getPointerElementType()->isSingleValueType());
    //bool ret = (type->isPointerTy() && type->getPointerElementType()->isFirstClassType());
    //std::cout << "access_via_load_store(): " << ret << ": type: " << type << std::endl;
    return ret;
}


/** Generate code to obtain field value, potentially based on a base value (pointer). */
static llvm::Value* field_value_code_gen(LlvmGenerationContext& context, GenScope* scope,
                                         llvm::Value* baseValue, const TxFieldEntity* entity) {
    llvm::Value* val = NULL;
    switch (entity->get_storage()) {
    case TXS_STATIC:
        // TODO: polymorphic lookup
    case TXS_GLOBAL:
    case TXS_STACK:
        if (auto constProxy = entity->get_static_constant_proxy()) {
            val = constProxy->code_gen(context, scope);
            context.LOG.debug("Generating field value code for statically constant entity %s: %s", entity->to_string().c_str(), ::to_string(val).c_str());
            break;
        }
        val = context.lookup_llvm_value(entity->get_full_name().to_string());
        if (! val) {
            if (auto txType = entity->get_type()) {
                // forward declaration situation
                if (auto txFuncType = dynamic_cast<const TxFunctionType*>(txType)) {
                    context.LOG.warning("Forward-declaring function %s", entity->get_full_name().to_string().c_str());
                    llvm::FunctionType *ftype = llvm::cast<llvm::FunctionType>(context.get_llvm_type(txFuncType));
                    val = context.llvmModule.getOrInsertFunction(entity->get_full_name().to_string(), ftype);
                    //llvm::cast<llvm::Function>(val)->setLinkage(llvm::GlobalValue::InternalLinkage);  FIXME (can cause LLVM to rename function)
                }
                else
                    context.LOG.error("No LLVM value defined for %s", entity->to_string().c_str());
            }
        }
        break;

    case TXS_INSTANCE:
        if (! baseValue)
            context.LOG.error("Attempted to dereference TXS_INSTANCE field but no base pointer provided (identifier %s)", entity->get_full_name().to_string().c_str());
        else {
            auto fieldIx = entity->get_instance_field_index();
            //std::cout << "Getting TXS_INSTANCE ix " << fieldIx << " value off LLVM base value: " << baseValue << std::endl;
            llvm::Value* ixs[] = { llvm::ConstantInt::get(llvm::Type::getInt32Ty(context.llvmContext), 0),
                                   llvm::ConstantInt::get(llvm::Type::getInt32Ty(context.llvmContext), fieldIx) };
            if (!scope)
                val = llvm::GetElementPtrInst::CreateInBounds(baseValue, ixs);
            else
                val = scope->builder->CreateInBoundsGEP(baseValue, ixs);
        }
        break;

    case TXS_NOSTORAGE:
        //context.LOG.error("TXS_NOSTORAGE specified for field: %s", entity->get_full_name().to_string().c_str());
        break;
    }
    return val;
}


llvm::Value* TxFieldValueNode::code_gen_address(LlvmGenerationContext& context, GenScope* scope) const {
    //return context.lookup_llvm_value(this->get_entity()->get_full_name().to_string());
    llvm::Value* value = NULL;
    if (this->base)
        value = this->base->code_gen(context, scope);
    for (auto sym : this->memberPath) {
        if (auto field = dynamic_cast<const TxFieldEntity*>(sym)) {
            value = field_value_code_gen(context, scope, value, field);
            if (sym != this->memberPath.back()) {  // skips the load for the last segment
                if ( value && access_via_load_store(value->getType()) ) {
                     //&& !( entity->get_storage() == TXS_STACK && !entity->is_modifiable() ) ) {
                    if (scope)
                        value = scope->builder->CreateLoad(value);
                    else
                        value = new llvm::LoadInst(value);
                }
            }
        }
        else {
            value = NULL;
        }
    }
    return value;
}

llvm::Value* TxFieldValueNode::code_gen(LlvmGenerationContext& context, GenScope* scope) const {
    context.LOG.trace("%-48s", this->to_string().c_str());
    llvm::Value* value = this->code_gen_address(context, scope);

    // Only function/complex pointers and non-modifiable temporaries don't require a load instruction:
    if ( value && access_via_load_store(value->getType()) ) {
         //&& !( entity->get_storage() == TXS_STACK && !entity->is_modifiable() ) ) {
        if (scope)
            value = scope->builder->CreateLoad(value);
        else {
           // in global scope we apparently don't want to load
           //value = new llvm::LoadInst(value);
//            if (auto constant = llvm::dyn_cast<llvm::Constant>(value)) {
//            }
        }
    }
    return value;
}


llvm::Value* TxFieldAssigneeNode::code_gen(LlvmGenerationContext& context, GenScope* scope) const {
    context.LOG.trace("%-48s", this->to_string().c_str());
    llvm::Value* value = NULL;
    if (this->base)
        value = this->base->code_gen(context, scope);
    for (auto sym : this->memberPath) {
        if (auto field = dynamic_cast<const TxFieldEntity*>(sym))
            value = field_value_code_gen(context, scope, value, field);
        else
            value = NULL;
    }
    return value;
}

