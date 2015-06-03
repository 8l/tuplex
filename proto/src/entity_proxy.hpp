#pragma once


class TxExpressionNode;
class TxType;
class TxField;
class ResolutionContext;


/** Proxy interface that provides a layer of indirection to a type reference.
 * This is needed for two reasons:
 * - Before the symbol table pass has completed, resolving the actual type may not be possible
 * - Resolving the type may be context-dependent (e.g. type parameter resolution depends
 *   on subtype context)
 */
class TxTypeProxy {
public:
    virtual ~TxTypeProxy() = default;

    /** Gets the TxType instance this type proxy represents.
     * The contract is that it shall return the same instance every invocation.
     */
    virtual const TxType* get_type() const = 0;
};


class TxEntityDefiner {
public:
    virtual ~TxEntityDefiner() = default;
};

class TxTypeDefiner : public TxTypeProxy, public TxEntityDefiner {
public:
    virtual const TxType* resolve_type(ResolutionContext& resCtx) = 0;

    /** Returns a type if this type definer "is ready" (has a defined type), otherwise NULL. */
    virtual const TxType* attempt_get_type() const = 0;
};

class TxFieldDefiner : public TxTypeDefiner {
public:
    /** Gets the TxExpressionNode that defines the initialization value for this field.
     * Returns nullptr if there is no initializer.
     */
    virtual const TxExpressionNode* get_init_expression() const = 0;

    virtual const TxField* resolve_field(ResolutionContext& resCtx) = 0;

    virtual const TxField* get_field() const = 0;


    virtual const TxType* resolve_type(ResolutionContext& resCtx) override;

    //virtual const TxType* attempt_get_type() const override = 0;
};
