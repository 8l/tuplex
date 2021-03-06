#include "tx_error.hpp"

#include "ast/ast_node.hpp"
#include "parsercontext.hpp"


const TxLocation& TxParseOrigin::get_parse_location() const {
    return this->get_origin_node()->get_parse_location();
}

ExpectedErrorClause* TxParseOrigin::exp_err_ctx() const {
    return this->get_origin_node()->exp_err_ctx();
}

ScopedExpErrClause::ScopedExpErrClause( TxParseOrigin* origin, bool enabled )
        : origin( origin ), enabled( enabled ) {
    if ( this->enabled )
        origin->get_parser_context()->begin_exp_err( origin );
}

ScopedExpErrClause::~ScopedExpErrClause() {
    if ( this->enabled )
        origin->get_parser_context()->end_exp_err( origin->get_parse_location() );
}

void cerror( const TxParseOrigin* origin, const std::string& msg ) {
    origin->get_parser_context()->cerror( origin, msg );
}

void cwarning( const TxParseOrigin* origin, const std::string& msg ) {
    origin->get_parser_context()->cwarning( origin, msg );
}

void cerror( const TxParseOrigin& origin, const std::string& msg ) {
    cerror( &origin, msg );
}

void cwarning( const TxParseOrigin& origin, const std::string& msg ) {
    cwarning( &origin, msg );
}

void finalize_expected_error_clause( const TxParseOrigin* origin ) {
    auto expError = origin->exp_err_ctx();
    // Note: Can't use origin directly in these error messages since origin holds an ExpectedErrorContext.
    if ( expError->expected_error_count < 0 ) {
        if ( expError->encountered_error_count == 0 ) {
            std::stringstream msg;
            msg << "COMPILER TEST FAIL: Expected one or more compilation errors but encountered " << expError->encountered_error_count;
            origin->get_parser_context()->cerror( origin->get_parse_location(), msg.str() );
        }
    }
    else if ( expError->expected_error_count != expError->encountered_error_count ) {
        std::stringstream msg;
        msg << "COMPILER TEST FAIL: Expected " << expError->expected_error_count << " compilation errors but encountered "
            << expError->encountered_error_count;
        origin->get_parser_context()->cerror( origin->get_parse_location(), msg.str() );
    }
}
