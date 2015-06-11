#pragma once

#include "ast_base.hpp"
//#include "ast_exprs.hpp"
class TxFunctionCallNode;


/** Local field declaration */
class TxFieldStmtNode : public TxStatementNode {
public:
    TxFieldDefNode* field;

    TxFieldStmtNode(const yy::location& parseLocation, TxFieldDefNode* field)
        : TxStatementNode(parseLocation), field(field) { }

    virtual void symbol_declaration_pass(TxSpecializationIndex six, LexicalContext& lexContext) override {
        this->field->symbol_declaration_pass_local_field(six, lexContext, true);
        this->set_context(six, this->field->context(six));
    }

    virtual void symbol_resolution_pass(TxSpecializationIndex six, ResolutionContext& resCtx) override {
        this->field->symbol_resolution_pass(six, resCtx);
    }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;
};

/** Local type declaration */
class TxTypeStmtNode : public TxStatementNode {
public:
    TxTypeDeclNode* const typeDecl;

    TxTypeStmtNode(const yy::location& parseLocation, const std::string typeName,
                   const std::vector<TxDeclarationNode*>* typeParamDecls, TxTypeExpressionNode* typeExpression)
        : TxStatementNode(parseLocation),
          typeDecl(new TxTypeDeclNode(parseLocation, TXD_NONE, typeName, typeParamDecls, typeExpression))
    { }

    virtual void symbol_declaration_pass(TxSpecializationIndex six, LexicalContext& lexContext) override {
        this->typeDecl->symbol_declaration_pass(six, lexContext);
        this->set_context(six, this->typeDecl->context(six));
    }

    virtual void symbol_resolution_pass(TxSpecializationIndex six, ResolutionContext& resCtx) override {
        this->typeDecl->symbol_resolution_pass(six, resCtx);
    }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;
};

class TxCallStmtNode : public TxStatementNode {  // function call without assigning return value (if any)
public:
    TxFunctionCallNode* call;

    TxCallStmtNode(const yy::location& parseLocation, TxFunctionCallNode* call)
        : TxStatementNode(parseLocation), call(call) { }

    virtual void symbol_declaration_pass(TxSpecializationIndex six, LexicalContext& lexContext) override {
        this->set_context(six, lexContext);
        ((TxExpressionNode*)this->call)->symbol_declaration_pass(six, lexContext);
    }

    virtual void symbol_resolution_pass(TxSpecializationIndex six, ResolutionContext& resCtx) override {
        ((TxExpressionNode*)this->call)->symbol_resolution_pass(six, resCtx);
    }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;
};

class TxTerminalStmtNode : public TxStatementNode {
protected:
    TxTerminalStmtNode(const yy::location& parseLocation) : TxStatementNode(parseLocation)  { }
};

class TxReturnStmtNode : public TxTerminalStmtNode {
public:
    TxExpressionNode* expr;

    TxReturnStmtNode(const yy::location& parseLocation, TxExpressionNode* expr)
        : TxTerminalStmtNode(parseLocation), expr(expr) { }

    virtual void symbol_declaration_pass(TxSpecializationIndex six, LexicalContext& lexContext) override {
        this->set_context(six, lexContext);
        if (this->expr)
            this->expr->symbol_declaration_pass(six, lexContext);
    }

    virtual void symbol_resolution_pass(TxSpecializationIndex six, ResolutionContext& resCtx) override {
        // TODO: Fix so that this won't find false positive using outer function's $return typeDecl
        // TODO: Illegal to return reference to STACK dataspace
        auto returnDecl = lookup_field(this->context(six).scope(), TxIdentifier("$return"));
        if (this->expr) {
            this->expr->symbol_resolution_pass(six, resCtx);
            if (returnDecl)
                this->expr = validate_wrap_convert(six, resCtx, this->expr, returnDecl->get_definer()->resolve_field(resCtx)->get_type());
            else
                CERROR(this, "Return statement has value expression although function has no return type");
        }
        else if (returnDecl)
            CERROR(this, "Return statement has no value expression although function returns " << returnDecl);
    }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;
};

class TxBreakStmtNode : public TxTerminalStmtNode {
public:
    TxBreakStmtNode(const yy::location& parseLocation) : TxTerminalStmtNode(parseLocation)  { }

    virtual void symbol_declaration_pass(TxSpecializationIndex six, LexicalContext& lexContext) override { this->set_context(six, lexContext); }
    virtual void symbol_resolution_pass(TxSpecializationIndex six, ResolutionContext& resCtx) override { }
    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;
};

class TxContinueStmtNode : public TxTerminalStmtNode {
public:
    TxContinueStmtNode(const yy::location& parseLocation) : TxTerminalStmtNode(parseLocation)  { }

    virtual void symbol_declaration_pass(TxSpecializationIndex six, LexicalContext& lexContext) override { this->set_context(six, lexContext); }
    virtual void symbol_resolution_pass(TxSpecializationIndex six, ResolutionContext& resCtx) override { }
    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;
};


class TxSuiteNode : public TxStatementNode {
public:
    std::vector<TxStatementNode*>* suite;

    TxSuiteNode(const yy::location& parseLocation);
    TxSuiteNode(const yy::location& parseLocation, std::vector<TxStatementNode*>* suite);

    virtual void symbol_declaration_pass_no_subscope(TxSpecializationIndex six, LexicalContext& lexContext) {
        this->set_context(six, lexContext);
        for (auto stmt : *this->suite)
            stmt->symbol_declaration_pass(six, lexContext);
    }
    virtual void symbol_declaration_pass(TxSpecializationIndex six, LexicalContext& lexContext) override {
        LexicalContext suiteContext(lexContext.scope()->create_code_block_scope(), lexContext.get_constructed());
        this->symbol_declaration_pass_no_subscope(six, suiteContext);
    }

    virtual void symbol_resolution_pass(TxSpecializationIndex six, ResolutionContext& resCtx) override {
        TxStatementNode* prev_stmt = nullptr;
        for (auto stmt : *this->suite) {
            if (dynamic_cast<TxTerminalStmtNode*>(prev_stmt))
                CERROR(stmt, "This statement is unreachable.");
            stmt->symbol_resolution_pass(six, resCtx);
            prev_stmt = stmt;
        }
    }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;
};


class TxElseClauseNode : public TxStatementNode {
public:
    TxStatementNode* suite;

    TxElseClauseNode(const yy::location& parseLocation, TxStatementNode* suite)
        : TxStatementNode(parseLocation), suite(suite)  { }

    virtual void symbol_declaration_pass(TxSpecializationIndex six, LexicalContext& lexContext) override {
        this->set_context(six, lexContext);
        this->suite->symbol_declaration_pass(six, lexContext);
    }

    virtual void symbol_resolution_pass(TxSpecializationIndex six, ResolutionContext& resCtx) override {
        this->suite->symbol_resolution_pass(six, resCtx);
    }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;
};

class TxCondCompoundStmtNode : public TxStatementNode {
protected:
    TxExpressionNode* cond;
    TxStatementNode* suite;
    TxElseClauseNode* elseClause;

public:
    TxCondCompoundStmtNode(const yy::location& parseLocation, TxExpressionNode* cond, TxStatementNode* suite,
                           TxElseClauseNode* elseClause=nullptr)
        : TxStatementNode(parseLocation), cond(cond), suite(suite), elseClause(elseClause)  { }

    virtual void symbol_declaration_pass(TxSpecializationIndex six, LexicalContext& lexContext) override {
        this->set_context(six, lexContext);
        this->cond->symbol_declaration_pass(six, lexContext);
        this->suite->symbol_declaration_pass(six, lexContext);
        if (this->elseClause)
            this->elseClause->symbol_declaration_pass(six, lexContext);
    }

    virtual void symbol_resolution_pass(TxSpecializationIndex six, ResolutionContext& resCtx) override {
        this->cond->symbol_resolution_pass(six, resCtx);
        this->cond = validate_wrap_convert(six, resCtx, this->cond, this->types().get_builtin_type(BOOL));
        this->suite->symbol_resolution_pass(six, resCtx);
        if (this->elseClause)
            this->elseClause->symbol_resolution_pass(six, resCtx);
    }
};

class TxIfStmtNode : public TxCondCompoundStmtNode {
public:
    TxIfStmtNode(const yy::location& parseLocation, TxExpressionNode* cond, TxStatementNode* suite,
                 TxElseClauseNode* elseClause=nullptr)
        : TxCondCompoundStmtNode(parseLocation, cond, suite, elseClause)  { }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;
};

class TxWhileStmtNode : public TxCondCompoundStmtNode {
public:
    TxWhileStmtNode(const yy::location& parseLocation, TxExpressionNode* cond, TxStatementNode* suite,
                    TxElseClauseNode* elseClause=nullptr)
        : TxCondCompoundStmtNode(parseLocation, cond, suite, elseClause)  { }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;
};



class TxAssignStmtNode : public TxStatementNode {
public:
    TxAssigneeNode* lvalue;
    TxExpressionNode* rvalue;

    TxAssignStmtNode(const yy::location& parseLocation, TxAssigneeNode* lvalue, TxExpressionNode* rvalue)
        : TxStatementNode(parseLocation), lvalue(lvalue), rvalue(rvalue)  { }

    virtual void symbol_declaration_pass(TxSpecializationIndex six, LexicalContext& lexContext) override {
        this->set_context(six, lexContext);
        this->lvalue->symbol_declaration_pass(six, lexContext);
        this->rvalue->symbol_declaration_pass(six, lexContext);
    }

    virtual void symbol_resolution_pass(TxSpecializationIndex six, ResolutionContext& resCtx) override {
        this->lvalue->symbol_resolution_pass(six, resCtx);
        this->rvalue->symbol_resolution_pass(six, resCtx);
        auto ltype = this->lvalue->resolve_type(six, resCtx);
        if (! ltype)
            return;  // (error message should have been emitted by lvalue node)
        if (! ltype->is_modifiable()) {
            if (! this->context(six).get_constructed())  // TODO: only members of constructed object should skip error
                CERROR(this, "Assignee is not modifiable: " << ltype);
            // Note: If the object as a whole is modifiable, it can be assigned to.
            // If it has any "non-modifiable" members, those will still get overwritten.
            // We could add custom check to prevent that scenario for Arrays, but then
            // it would in this regard behave differently than other aggregate objects.
        }
        // note: similar rules to passing function arg
        this->rvalue = validate_wrap_assignment(six, resCtx, this->rvalue, ltype);
    }

    virtual llvm::Value* code_gen(LlvmGenerationContext& context, GenScope* scope) const override;
};
