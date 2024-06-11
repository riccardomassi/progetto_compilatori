%skeleton "lalr1.cc" /* -*- C++ -*- */
%require "3.2"
%defines

%define api.token.constructor
%define api.location.file none
%define api.value.type variant
%define parse.assert

%code requires {
  #include <string>
  #include <exception>
  class driver;
  class RootAST;
  class ExprAST;
  class NumberExprAST;
  class VariableExprAST;
  class CallExprAST;
  class FunctionAST;
  class SeqAST;
  class PrototypeAST;
  class BlockExprAST;
  class VarBindingAST;
  class VarGlobalAST;
  class VarAssignAST;
  class ForExprAST;
  class IfExprAST;
  class BoolExprAST;
}

// The parsing context.
%param { driver& drv }

%locations

%define parse.trace
%define parse.error verbose

%code {
# include "driver.hpp"
}

%define api.token.prefix {TOK_}
%token
  END  0  "end of file"
  SEMICOLON  ";"
  COMMA      ","
  MINUS      "-"
  PLUS       "+"
  STAR       "*"
  SLASH      "/"
  LPAREN     "("
  RPAREN     ")"
  QMARK	     "?"
  COLON      ":"
  LT         "<"
  EQ         "=="
  ASSIGN     "="
  LBRACE     "{"
  RBRACE     "}"
  LBRACKET   "["
  RBRACKET   "]"
  EXTERN     "extern"
  DEF        "def"
  VAR        "var"
  GLOBAL     "global"
  IF         "if"
  ELSE       "else"
  FOR        "for"
  AND        "and"
  OR         "or"
  NOT        "not"
;

%token <std::string> IDENTIFIER "id"
%token <double> NUMBER "number"
%type <ExprAST*> exp
%type <ExprAST*> idexp
%type <ExprAST*> expif
%type <ExprAST*> condexp
%type <ExprAST*> relexp
%type <std::vector<ExprAST*>> optexp
%type <std::vector<ExprAST*>> explist
%type <RootAST*> program
%type <RootAST*> top
%type <FunctionAST*> definition
%type <PrototypeAST*> external
%type <PrototypeAST*> proto
%type <std::vector<std::string>> idseq
%type <BlockExprAST*> block
%type <std::vector<VarBindingAST*>> vardefs
%type <VarBindingAST*> binding
%type <VarGlobalAST*> globalvar
%type <ExprAST*> stmt
%type <std::vector<ExprAST*>> stmts 
%type <VarAssignAST*> assignment
%type <ExprAST*> initexp
%type <IfExprAST*> ifstmt
%type <ForExprAST*> forstmt
%type <RootAST*> init

%%
%start startsymb;

startsymb:
  program                 { drv.root = $1; };

program:
  %empty                { $$ = new SeqAST(nullptr,nullptr); }
|  top ";" program      { $$ = new SeqAST($1,$3); };

top:
  %empty                { $$ = nullptr; }
| definition            { $$ = $1; }
| external              { $$ = $1; }
| globalvar             { $$ = $1; };

definition:
  "def" proto block     { $$ = new FunctionAST($2,$3); $2->noemit(); };

external:
  "extern" proto        { $$ = $2; };

proto:
  "id" "(" idseq ")"    { $$ = new PrototypeAST($1,$3);  };

globalvar:
  "global" "id"                   { $$ = new VarGlobalAST($2); }
| "global" "id" "[" "number" "]"  { $$ = new VarGlobalAST($2,$4); }; 

idseq:
  %empty                { std::vector<std::string> args;
                         $$ = args; }
| "id" idseq            { $2.insert($2.begin(),$1); $$ = $2; };

%left ":";
%left "<" "==";
%left "+" "-";
%left "*" "/";

stmts:
  stmt            { $$ = std::vector<ExprAST*>{ $1 }; }          
| stmt ";" stmts  { $3.insert($3.begin(),$1); 
                    $$ = $3; };

stmt:
  assignment  { $$ = $1; }
| block       { $$ = $1; }
| ifstmt      { $$ = $1; }
| forstmt     { $$ = $1; }
| exp         { $$ = $1; };

ifstmt:
  "if" "(" condexp ")" stmt             { $$ = new IfExprAST($3,$5,nullptr); }   
| "if" "(" condexp ")" stmt "else" stmt { $$ = new IfExprAST($3,$5,$7); }

forstmt:
  "for" "(" init ";" condexp ";" assignment ")" stmt  { $$ = new ForExprAST($3,$5,$7,$9); }

init:
  binding     { $$ = $1; }
| assignment  { $$ = $1; }

assignment:
  "id" "=" exp              { $$ = new VarAssignAST($1,$3); }
| "id" "[" exp "]" "=" exp  { $$ = new VarAssignAST($1,$6,$3); }
| "+" "+" "id"              { $$ = new VarAssignAST($3,new BinaryExprAST('+',new VariableExprAST($3),new NumberExprAST(1.0))); }
| "-" "-" "id"              { $$ = new VarAssignAST($3,new BinaryExprAST('-',new VariableExprAST($3),new NumberExprAST(1.0))); }

block:
  "{" stmts "}"             { $$ = new BlockExprAST(std::vector<VarBindingAST*>(),$2); }
| "{" vardefs ";" stmts "}" { $$ = new BlockExprAST($2,$4); };
  
vardefs:
  binding                 { std::vector<VarBindingAST*> definitions;
                            definitions.push_back($1);
                            $$ = definitions; }
| vardefs ";" binding     { $1.push_back($3);
                            $$ = $1; };
                            
binding:
  "var" "id" initexp                              { $$ = new VarBindingAST($2,$3); };
| "var" "id" "[" "number" "]"                     { $$ = new VarBindingAST($2,nullptr,$4); }
| "var" "id" "[" "number" "]" "=" "{" explist "}" { $$ = new VarBindingAST($2,nullptr,$4,$8); } 

exp:
  "-" exp               { $$ = new BinaryExprAST('-',new NumberExprAST(0.0),$2); }
| exp "+" exp           { $$ = new BinaryExprAST('+',$1,$3); }
| exp "-" exp           { $$ = new BinaryExprAST('-',$1,$3); }
| exp "*" exp           { $$ = new BinaryExprAST('*',$1,$3); }
| exp "/" exp           { $$ = new BinaryExprAST('/',$1,$3); }
| idexp                 { $$ = $1; }
| "(" exp ")"           { $$ = $2; }
| "number"              { $$ = new NumberExprAST($1); }
| expif                 { $$ = $1; };

initexp:
  %empty  { $$ = nullptr; }
| "=" exp { $$ = $2; };
                      
expif:
  condexp "?" exp ":" exp { $$ = new IfExprAST($1,$3,$5); };

condexp :
  relexp                { $$ = $1; }
| relexp "and" condexp  { $$ = new BoolExprAST("and",$1,$3); }
| relexp "or" condexp   { $$ = new BoolExprAST("or",$1,$3); }
| "not" condexp         { $$ = new BoolExprAST("not",$2); }
| "(" condexp ")"       { $$ = $2; }

relexp :
  exp "<" exp           { $$ = new BinaryExprAST('<',$1,$3); }
| exp "==" exp          { $$ = new BinaryExprAST('=',$1,$3); };

idexp:
  "id"                  { $$ = new VariableExprAST($1); }
| "id" "(" optexp ")"   { $$ = new CallExprAST($1,$3); };
| "id" "[" exp "]"      { $$ = new VariableExprAST($1,$3, true); }

optexp:
  %empty                { std::vector<ExprAST*> args; 
                          $$ = args; }
| explist               { $$ = $1; };

explist:
  exp                   { $$ = std::vector<ExprAST*>{ $1 }; }
| exp "," explist       { $3.insert($3.begin(), $1); 
                          $$ = $3; };
 
%%

void
yy::parser::error (const location_type& l, const std::string& m)
{
  std::cerr << l << ": " << m << '\n';
}
