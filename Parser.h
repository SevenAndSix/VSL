#ifndef __PARSER_H__
#define __PARSER_H__
#include "AST.h"
#include "Lexer.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Verifier.h"
using namespace llvm;

static int CurTok;
static std::map<char, int> BinopPrecedence;
static int getNextToken() { return CurTok = gettok(); }

static std::unique_ptr<StatAST> ParseExpression();
std::unique_ptr<StatAST> LogError(const char *Str);
static std::unique_ptr<StatAST> ParseNumberExpr();
static std::unique_ptr<StatAST> ParseParenExpr();
static std::unique_ptr<DecAST> ParseDec();
std::unique_ptr<StatAST> LogError(const char *Str);
std::unique_ptr<PrototypeAST> LogErrorP(const char *Str);
std::unique_ptr<StatAST> LogErrorS(const char *Str);
std::unique_ptr<DecAST> LogErrorD(const char *Str);
static std::unique_ptr<StatAST> ParseStatement();

//解析如下格式的表达式：
// identifer || identifier(expression list)
static std::unique_ptr<StatAST> ParseIdentifierExpr() {
	std::string IdName = IdentifierStr;

	getNextToken();

	//解析成变量表达式
	if (CurTok != '(')
		return llvm::make_unique<VariableExprAST>(IdName);

	// 解析成函数调用表达式
	getNextToken();
	std::vector<std::unique_ptr<StatAST>> Args;
	if (CurTok != ')') {
		while (true) {
			if (auto Arg = ParseExpression())
				Args.push_back(std::move(Arg));
			else
				return nullptr;

			if (CurTok == ')')
				break;

			if (CurTok != ',')
				return LogErrorS("Expected ')' or ',' in argument list");
			getNextToken();
		}
	}

	getNextToken();

	return llvm::make_unique<CallExprAST>(IdName, std::move(Args));
}

//解析取反表达式
static std::unique_ptr<StatAST> ParseNegExpr() {
	getNextToken();
	std::unique_ptr<StatAST> Exp = ParseExpression();
	if (!Exp)
		return nullptr;

	return llvm::make_unique<NegExprAST>(std::move(Exp));
}

//解析成 标识符表达式、整数表达式、括号表达式中的一种
static std::unique_ptr<StatAST> ParsePrimary() {
	switch (CurTok) {
	default:
		return LogError("unknown token when expecting an expression");
	case VARIABLE:
		return ParseIdentifierExpr();
	case INTEGER:
		return ParseNumberExpr();
	case '(':
		return ParseParenExpr();
	case '-':
		return ParseNegExpr();
	}
}

//GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;

  // Make sure it's a declared binop.
  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0)
    return -1;
  return TokPrec;
}

//解析二元表达式
//参数 ：
//ExprPrec 左部运算符优先级
//LHS 左部操作数
// 递归得到可以结合的右部，循环得到一个整体二元表达式
static std::unique_ptr<StatAST> ParseBinOpRHS(int ExprPrec,
	std::unique_ptr<StatAST> LHS) {

	while (true) {
		int TokPrec = GetTokPrecedence();

		// 当右部没有运算符或右部运算符优先级小于左部运算符优先级时 退出循环和递归
		if (TokPrec < ExprPrec)
			return LHS;

		if(CurTok == '}')
			return LHS;

		// 保存左部运算符
		int BinOp = CurTok;
		getNextToken();

		// 得到右部表达式
		auto RHS = ParsePrimary();
		if (!RHS)
			return nullptr;

		// 如果该右部表达式不与该左部表达式结合 那么递归得到右部表达式
		int NextPrec = GetTokPrecedence();
		if (TokPrec < NextPrec) {
			RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
			if (!RHS)
				return nullptr;
		}

		// 将左右部结合成新的左部
		LHS = llvm::make_unique<BinaryExprAST>(BinOp, std::move(LHS),
			std::move(RHS));
	}
}

// 解析得到表达式
static std::unique_ptr<StatAST> ParseExpression() {
	auto LHS = ParsePrimary();
	if (!LHS)
		return nullptr;

	return ParseBinOpRHS(0, std::move(LHS));
}

// numberexpr ::= number
static std::unique_ptr<StatAST> ParseNumberExpr() {
	auto Result = llvm::make_unique<NumberExprAST>(NumberVal);
	//略过数字获取下一个输入
	getNextToken();
	return std::move(Result);
}

//declaration::=VAR variable_list
static std::unique_ptr<DecAST> ParseDec() {
	//eat 'VAR'
	getNextToken();

	std::vector<std::string> varNames;
	//保证至少有一个变量的名字
	if (CurTok != VARIABLE) {
		return LogErrorD("expected identifier after VAR");
	}

	while (true)
	{
		varNames.push_back(IdentifierStr);
		//eat VARIABLE
		getNextToken();
		if (CurTok != ',')
			break;
		getNextToken();
		if (CurTok != VARIABLE) {
			return LogErrorD("expected identifier list after VAR");
		}
	}

	auto Body = nullptr;

	return llvm::make_unique<DecAST>(std::move(varNames), std::move(Body));
}

//null_statement::=CONTINUE
static std::unique_ptr<StatAST> ParseNullStat() {
	getNextToken();
	return llvm::make_unique<NullStatAST>();
}

//block::='{' declaration_list statement_list '}'
static std::unique_ptr<StatAST> ParseBlock() {
	//存储变量声明语句及其他语句
	std::vector<std::unique_ptr<DecAST>> DecList;
	std::vector<std::unique_ptr<StatAST>> StatList;
	getNextToken();   //eat '{'
	if (CurTok == VAR) {
		auto varDec = ParseDec();
		DecList.push_back(std::move(varDec));
	}
	while (CurTok != '}') {
		if (CurTok == VAR) {
			LogErrorS("Can't declare VAR here!");
		}
		else if (CurTok == '{') {
			ParseBlock();
		}
		else if (CurTok == CONTINUE) {
			getNextToken();
		}
		else {
			auto statResult = ParseStatement();
			StatList.push_back(std::move(statResult));
		}
	}
	getNextToken();  //eat '}'

	return llvm::make_unique<BlockStatAST>(std::move(DecList), std::move(StatList));
}

//prototype ::= VARIABLE '(' parameter_list ')'
static std::unique_ptr<PrototypeAST> ParsePrototype() {
	if (CurTok != VARIABLE)
		return LogErrorP("Expected function name in prototype");

	std::string FnName = IdentifierStr;
	getNextToken();

	if (CurTok != '(')
		return LogErrorP("Expected '(' in prototype");

	std::vector<std::string> ArgNames;
	getNextToken();
	while (CurTok == VARIABLE)
	{
		ArgNames.push_back(IdentifierStr);
		getNextToken();
		if (CurTok == ',')
			getNextToken();
	}
	if (CurTok != ')')
		return LogErrorP("Expected ')' in prototype");

	// success.
	getNextToken(); // eat ')'.

	return llvm::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

//function ::= FUNC VARIABLE '(' parameter_lst ')' statement
static std::unique_ptr<FunctionAST> ParseFunc()
{
	getNextToken(); // eat FUNC.
	auto Proto = ParsePrototype();
	if (!Proto)
		return nullptr;

	auto E = ParseStatement();
	if (!E)
		return nullptr;

	return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(E));
}

//解析括号中的表达式
static std::unique_ptr<StatAST> ParseParenExpr() {
	// 过滤'('
	getNextToken();
	auto V = ParseExpression();
	if (!V)
		return nullptr;

	if (CurTok != ')')
		return LogError("expected ')'");
	// 过滤')'
	getNextToken();
	return V;
}

//解析 IF Statement
static std::unique_ptr<StatAST> ParseIfStat() {
	getNextToken(); // eat the IF.

					// condition.
	auto Cond = ParseExpression();
	if (!Cond)
		return nullptr;

	if (CurTok != THEN)
		return LogErrorS("expected THEN");
	getNextToken(); // eat the THEN

	auto Then = ParseStatement();
	if (!Then)
		return nullptr;

	std::unique_ptr<StatAST> Else = nullptr;
	if (CurTok == ELSE) {
        getNextToken();
		Else = ParseStatement();
		if (!Else)
			return nullptr;
	}
	else if(CurTok != FI)
		return LogErrorS("expected FI or ELSE");

	getNextToken();

	return llvm::make_unique<IfStatAST>(std::move(Cond), std::move(Then),
		std::move(Else));
}

//PRINT,能输出变量和函数调用的值
static std::unique_ptr<StatAST> ParsePrintStat()
{
    std::string text = "";
	std::vector<std::unique_ptr<StatAST>> expr;
	getNextToken();//eat PRINT

    while(CurTok == VARIABLE || CurTok == TEXT || CurTok == '('
            || CurTok == '-' || CurTok == INTEGER)
    {
        if(CurTok == TEXT)
        {
            text += IdentifierStr;
            getNextToken();
        }
        else
        {
            text += " %d ";
			expr.push_back(std::move(ParseExpression()));
		}

        if(CurTok != ',')
            break;
        getNextToken(); //eat ','
    }

    return llvm::make_unique<PrintStatAST>(text, std::move(expr));
}

//解析 RETURN Statement
static std::unique_ptr<StatAST> ParseRetStat() {
	getNextToken();
	auto Val = ParseExpression();
	if (!Val)
		return nullptr;

	return llvm::make_unique<RetStatAST>(std::move(Val));
}

//解析 赋值语句
static std::unique_ptr<StatAST> ParseAssStat() {
	auto a = ParseIdentifierExpr();
	VariableExprAST* Name = (VariableExprAST*)a.get();
	auto NameV = llvm::make_unique<VariableExprAST>(Name->getName());
	if (!Name)
		return nullptr;
	if (CurTok != ASSIGN_SYMBOL)
		return LogErrorS("need := in assignment statment");
	getNextToken();

	auto Expression = ParseExpression();
	if (!Expression)
		return nullptr;

	return llvm::make_unique<AssStatAST>(std::move(NameV), std::move(Expression));
}

//解析while语句
static std::unique_ptr<StatAST> ParseWhileStat()
{
	getNextToken();//eat WHILE

	auto E = ParseExpression();
	if(!E)
		return nullptr;

	if(CurTok != DO)
		return LogErrorS("expect DO in WHILE statement");
	getNextToken();//eat DO

	auto S = ParseStatement();
	if(!S)
	return nullptr;

	if(CurTok != DONE)
		return LogErrorS("expect DONE in WHILE statement");
	getNextToken();//eat DONE

	return llvm::make_unique<WhileStatAST>(std::move(E), std::move(S));
}

static std::unique_ptr<StatAST> ParseStatement()
{
	switch (CurTok) {
		case IF:
			return ParseIfStat();
			break;
        case PRINT:
            return ParsePrintStat();
		case RETURN:
			return ParseRetStat();
		case VAR:
			return ParseDec();
			break;
		case '{':
			return ParseBlock();
			break;
		case CONTINUE:
			return ParseNullStat();
		case WHILE:
			return ParseWhileStat();
			break;
		default:
			auto E = ParseAssStat();
			return E;
	}
}

//解析程序结构
static std::unique_ptr<ProgramAST> ParseProgramAST() {
	//接受程序中函数的语法树
	std::vector<std::unique_ptr<FunctionAST>> Functions;

	//循环解析程序中所有函数
	while (CurTok != TOK_EOF) {
		auto Func=ParseFunc();
		Functions.push_back(std::move(Func));
	}

	return llvm::make_unique<ProgramAST>(std::move(Functions));
}

//错误信息打印
std::unique_ptr<StatAST> LogError(const char *Str) {
	fprintf(stderr, "Error: %s\n", Str);
	return nullptr;
}
std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
	LogError(Str);
	return nullptr;
}
std::unique_ptr<StatAST> LogErrorS(const char *Str) {
	fprintf(stderr, "Error: %s\n", Str);
	return nullptr;
}
std::unique_ptr<DecAST> LogErrorD(const char *Str) {
	fprintf(stderr, "Error: %s\n", Str);
	return nullptr;
}

// Top-Level parsing
static void HandleFuncDefinition() {
	if (auto FnAST = ParseFunc()) {
		FnAST->codegen();
	}
	else {
		// Skip token for error recovery.
		getNextToken();
	}
}

//声明printf函数
static void DeclarePrintfFunc()
{
	std::vector<llvm::Type *> printf_arg_types;
	printf_arg_types.push_back(Builder.getInt8Ty()->getPointerTo());
	FunctionType *printType = FunctionType::get(
		IntegerType::getInt32Ty(TheContext), printf_arg_types, true);
	printFunc = llvm::Function::Create(printType, llvm::Function::ExternalLinkage,
									   llvm::Twine("printf"), TheModule);
	printFunc->setCallingConv(llvm::CallingConv::C);

	std::vector<std::string> ArgNames;
	FunctionProtos["printf"] = std::move(llvm::make_unique<PrototypeAST>("printf", std::move(ArgNames)));
}

//program ::= function_list
static void MainLoop() {
	DeclarePrintfFunc();
	while(CurTok != TOK_EOF)
		HandleFuncDefinition();

	if (emitIR)
	{
		std::string IRstr;
		FILE *IRFile = fopen("IRCode.ll", "w");
		raw_string_ostream *rawStr = new raw_string_ostream(IRstr);
		TheModule->print(*rawStr, nullptr);
		fprintf(IRFile, "%s\n", rawStr->str().c_str());
	}

	if(!emitObj)
	{
		Function *main = getFunction("main");
		if (!main)
			printf("main is null");
		std::string errStr;
		ExecutionEngine *EE = EngineBuilder(std::move(Owner)).setErrorStr(&errStr).create();
		if (!EE)
		{
			errs() << "Failed to construct ExecutionEngine: " << errStr << "\n";
			return;
		}
		std::vector<GenericValue> noarg;
		GenericValue gv = EE->runFunction(main, noarg);
	}
}
#endif
