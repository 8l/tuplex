#include "ast_flow.hpp"

#include "ast/expr/ast_maybe_conv_node.hpp"
#include "ast/expr/ast_field.hpp"
#include "ast/expr/ast_exprs.hpp"


TxIsClauseNode::TxIsClauseNode( const TxLocation& ploc, TxExpressionNode* valueExpr, const std::string& valueName, TxTypeExpressionNode* typeExpr )
        : TxFlowHeaderNode( ploc ), origValueExpr( valueExpr ), valueName( valueName ), typeExpr( typeExpr )  {
    auto & loc = this->ploc;

    this->valueField = new TxLocalFieldDefNode( loc, this->valueName, this->typeExpr, this->origValueExpr, true );
}

void TxIsClauseNode::symbol_resolution_pass() {
    if ( this->origValueExpr->resolve_type()->get_type_class() != TXTC_REFERENCE )
        CERROR( this->origValueExpr, "Can't cast non-reference type: " << this->origValueExpr->qualtype() );

    if ( this->typeExpr->resolve_type()->get_type_class() != TXTC_REFERENCE )
        CERROR( this->typeExpr, "Can't cast non-reference type: " << this->typeExpr->qualtype() );
    else if ( this->typeExpr->qualtype()->type()->target_type()->get_type_class() == TXTC_INTERFACE )
        CERROR( this->typeExpr, "Cast to interface type currently not supported: " << this->typeExpr->qualtype()->type()->target_type() );

    this->valueField->symbol_resolution_pass();
}


TxInClauseNode::TxInClauseNode( const TxLocation& ploc, const std::string& valueName, const std::string& iterName,
                                TxExpressionNode* seqExpr )
        : TxFlowHeaderNode( ploc ), valueName( valueName ), iterName( iterName ), origSeqExpr( seqExpr ) {
    auto & loc = this->ploc;
    auto iterInitExpr = new TxFunctionCallNode( loc, new TxFieldValueNode( loc, new TxMaybeConversionNode( seqExpr ), "sequencer" ),
                                                new std::vector<TxExpressionNode*>() );
    this->iterField = new TxLocalFieldDefNode( loc, this->iterName, false, iterInitExpr );

    this->nextCond = new TxFunctionCallNode( loc, new TxFieldValueNode( loc, new TxFieldValueNode( loc, nullptr, this->iterName ), "has_next" ),
                                             new std::vector<TxExpressionNode*>() );

    auto nextValueExpr = new TxFunctionCallNode( loc, new TxFieldValueNode( loc, new TxFieldValueNode( loc, nullptr, this->iterName ), "next" ),
                                                 new std::vector<TxExpressionNode*>() );
    this->valueField = new TxLocalFieldDefNode( loc, this->valueName, false, nextValueExpr );
}
