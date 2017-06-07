#include "ast_stmts.hpp"

#include "ast/expr/ast_lambda_node.hpp"
#include "parsercontext.hpp"


void TxSuiteNode::stmt_declaration_pass() {
    if (! dynamic_cast<const TxLambdaExprNode*>(this->parent()))
        this->lexContext._scope = lexContext.scope()->create_code_block_scope( *this );
}

void TxAssignStmtNode::symbol_resolution_pass() {
    this->lvalue->symbol_resolution_pass();
    auto ltype = this->lvalue->resolve_type();

    // note: similar rules to passing function arg
    if ( !ltype->is_concrete() ) {
        if ( !this->context().is_generic() )
            CERROR( this->lvalue, "Assignee is not concrete: " << ltype );
        else
            LOG_DEBUG( this->LOGGER(), "(Not error since generic context) Assignee is not concrete: " << ltype );
    }

    if ( !( this->context().enclosing_lambda() && this->context().enclosing_lambda()->get_constructed() ) ) {
        // TODO: only members of constructed object should skip error
        if ( !lvalue->is_mutable() ) {
            // error message already generated
            //CERROR( this, "Assignee or assignee's container is not modifiable (nominal type of assignee is " << ltype << ")" );
        }
    }
    // Note: If the object as a whole is modifiable, it can be assigned to.
    // If it has any "non-modifiable" members, those will still get overwritten.
    // We could add custom check to prevent that scenario for Arrays, but then
    // it would in this regard behave differently than other aggregate objects.

    // if assignee is a reference:
    // TODO: check dataspace rules

    auto nonModLType = ( ltype->is_modifiable() ? ltype->get_base_type() : ltype );  // rvalue doesn't need to be modifiable
    this->rvalue->insert_conversion( nonModLType );
    this->rvalue->symbol_resolution_pass();
}

void TxExpErrStmtNode::stmt_declaration_pass() {
    //this->lexContext._scope = lexContext.scope()->create_code_block_scope( *this, "EE" );
    this->lexContext.expErrCtx = this->expError;
    if ( !this->context().is_reinterpretation() ) {
        this->get_parse_location().parserCtx->register_exp_err_node( this );
    }
}