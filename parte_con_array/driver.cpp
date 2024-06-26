#include "driver.hpp"
#include "parser.hpp"

// Generazione di un'istanza per ciascuna della classi LLVMContext,
// Module e IRBuilder. Nel caso di singolo modulo è sufficiente
LLVMContext *context = new LLVMContext;
Module *module = new Module("Kaleidoscope", *context);
IRBuilder<> *builder = new IRBuilder(*context);

Value *LogErrorV(const std::string Str) {
  std::cerr << Str << std::endl;
  return nullptr;
}

/* Il codice seguente sulle prime non è semplice da comprendere.
   Esso definisce una utility (funzione C++) con due parametri:
   1) la rappresentazione di una funzione llvm IR, e
   2) il nome per un registro SSA
   La chiamata di questa utility restituisce un'istruzione IR che alloca un double
   in memoria e ne memorizza il puntatore in un registro SSA cui viene attribuito
   il nome passato come secondo parametro. L'istruzione verrà scritta all'inizio
   dell'entry block della funzione passata come primo parametro.
   Si ricordi che le istruzioni sono generate da un builder. Per non
   interferire con il builder globale, la generazione viene dunque effettuata
   con un builder temporaneo TmpB
*/
static AllocaInst *CreateEntryBlockAlloca(Function *fun, StringRef VarName, Type *VarType = Type::getDoubleTy(*context)) {
  IRBuilder<> TmpB(&fun->getEntryBlock(), fun->getEntryBlock().begin());
  return TmpB.CreateAlloca(VarType, nullptr, VarName);
}

// Implementazione del costruttore della classe driver
driver::driver(): trace_parsing(false), trace_scanning(false) {};

// Implementazione del metodo parse
int driver::parse (const std::string &f) {
  file = f;                    // File con il programma
  location.initialize(&file);  // Inizializzazione dell'oggetto location
  scan_begin();                // Inizio scanning (ovvero apertura del file programma)
  yy::parser parser(*this);    // Istanziazione del parser
  parser.set_debug_level(trace_parsing); // Livello di debug del parsed
  int res = parser.parse();    // Chiamata dell'entry point del parser
  scan_end();                  // Fine scanning (ovvero chiusura del file programma)
  return res;
}

// Implementazione del metodo codegen, che è una "semplice" chiamata del 
// metodo omonimo presente nel nodo root (il puntatore root è stato scritto dal parser)
void driver::codegen() {
  root->codegen(*this);
};

/************************* Sequence tree **************************/
SeqAST::SeqAST(RootAST* first, RootAST* continuation):
  first(first), continuation(continuation) {};

// La generazione del codice per una sequenza è banale:
// mediante chiamate ricorsive viene generato il codice di first e 
// poi quello di continuation (con gli opportuni controlli di "esistenza")
Value *SeqAST::codegen(driver& drv) {
  if (first != nullptr) {
    Value *f = first->codegen(drv);
  } else {
    if (continuation == nullptr) return nullptr;
  }
  Value *c = continuation->codegen(drv);
  return nullptr;
};

/********************* Number Expression Tree *********************/
NumberExprAST::NumberExprAST(double Val): Val(Val) {};

lexval NumberExprAST::getLexVal() const {
  // Non utilizzata, Inserita per continuità con versione precedente
  lexval lval = Val;
  return lval;
};

// Non viene generata un'struzione; soltanto una costante LLVM IR
// corrispondente al valore float memorizzato nel nodo
// La costante verrà utilizzata in altra parte del processo di generazione
// Si noti che l'uso del contesto garantisce l'unicità della costanti 
Value *NumberExprAST::codegen(driver& drv) {  
  return ConstantFP::get(*context, APFloat(Val));
};

/******************** Variable Expression Tree ********************/
VariableExprAST::VariableExprAST(const std::string &Name, ExprAST *Index, bool isArray): 
  Name(Name), Index(Index), isArray(isArray) {};

lexval VariableExprAST::getLexVal() const {
  lexval lval = Name;
  return lval;
};

// NamedValues è una tabella che ad ogni variabile (che, in Kaleidoscope1.0, 
// può essere solo un parametro di funzione) associa non un valore bensì
// la rappresentazione di una funzione che alloca memoria e restituisce in un
// registro SSA il puntatore alla memoria allocata. Generare il codice corrispondente
// ad una varibile equivale dunque a recuperare il tipo della variabile 
// allocata e il nome del registro e generare una corrispondente istruzione di load
// Negli argomenti della CreateLoad ritroviamo quindi: (1) il tipo allocato, (2) il registro
// SSA in cui è stato messo il puntatore alla memoria allocata (si ricordi che A è
// l'istruzione ma è anche il registro, vista la corrispodenza 1-1 fra le due nozioni), (3)
// il nome del registro in cui verrà trasferito il valore dalla memoria
Value *VariableExprAST::codegen(driver& drv) {
  // Controlla se la variabile è un array
  if (isArray){
    // Controlla se l'indice è stato definito
    Value *IndexVal = Index->codegen(drv);
    if (!IndexVal)
      return nullptr;

    AllocaInst *A = drv.NamedValues[Name];

    // Converte l'indice in un intero
    Value *intIndexVal = builder->CreateFPToSI(IndexVal, Type::getInt32Ty(*context), "intIndexVal");

    if(!A){
      GlobalVariable *gvar = module->getGlobalVariable(Name);

      // Se l'array non è stato definito come globale, allora è un array non definito
      if(!gvar){
        return LogErrorV("Array "+Name+" non definito");
      } else {
        // Se l'array è stato definito come globale, allora si restituisce il valore all'indice
        Value *ElemPtr = builder->CreateInBoundsGEP(gvar->getValueType(), gvar, intIndexVal, Name + "_idx");
        return builder->CreateLoad(gvar->getValueType()->getArrayElementType(), ElemPtr, Name.c_str());
      }
    } else {
      // Se l'array è stato definito come parametro, allora si restituisce il valore all'indice
      Value *ElemPtr = builder->CreateInBoundsGEP(A->getAllocatedType(), A, intIndexVal, Name + "_idx");
      return builder->CreateLoad(A->getAllocatedType()->getArrayElementType(), ElemPtr, Name.c_str());
    }
  } else {
    AllocaInst *A = drv.NamedValues[Name];

    if(!A){
      // Se la variabile non è stata definita come parametro, allora è una variabile globale
      GlobalVariable *gvar = module->getGlobalVariable(Name);

      if(!gvar){
        // Se la variabile non è stata definita come globale, allora è una variabile non definita
        return LogErrorV("Variabile "+Name+" non definita");
      } else {
        // Se la variabile è stata definita come globale, allora si restituisce il valore
        return builder->CreateLoad(gvar->getValueType(), gvar, Name.c_str());
      }
    } else {
      // Se la variabile è stata definita come parametro, allora si restituisce il valore
      return builder->CreateLoad(A->getAllocatedType(), A, Name.c_str());
    }
  }

  return nullptr;
}

/******************** Binary Expression Tree **********************/
BinaryExprAST::BinaryExprAST(char Op, ExprAST* LHS, ExprAST* RHS):
  Op(Op), LHS(LHS), RHS(RHS) {};

// La generazione del codice in questo caso è di facile comprensione.
// Vengono ricorsivamente generati il codice per il primo e quello per il secondo
// operando. Con i valori memorizzati in altrettanti registri SSA si
// costruisce l'istruzione utilizzando l'opportuno operatore
Value *BinaryExprAST::codegen(driver& drv) {
  Value *L = LHS->codegen(drv);
  Value *R = RHS->codegen(drv);
  if (!L || !R) 
     return nullptr;
  switch (Op) {
  case '+':
    return builder->CreateFAdd(L,R,"addres");
  case '-':
    return builder->CreateFSub(L,R,"subres");
  case '*':
    return builder->CreateFMul(L,R,"mulres");
  case '/':
    return builder->CreateFDiv(L,R,"divres");
  case '<':
    return builder->CreateFCmpULT(L,R,"lttest");
  case '=':
    return builder->CreateFCmpUEQ(L,R,"eqtest");
  default:  
    std::cout << Op << std::endl;
    return LogErrorV("Operatore binario non supportato");
  }
};

/********************* Call Expression Tree ***********************/
/* Call Expression Tree */
CallExprAST::CallExprAST(std::string Callee, std::vector<ExprAST*> Args):
  Callee(Callee),  Args(std::move(Args)) {};

lexval CallExprAST::getLexVal() const {
  lexval lval = Callee;
  return lval;
};

Value* CallExprAST::codegen(driver& drv) {
  // La generazione del codice corrispondente ad una chiamata di funzione
  // inizia cercando nel modulo corrente (l'unico, nel nostro caso) una funzione
  // il cui nome coincide con il nome memorizzato nel nodo dell'AST
  // Se la funzione non viene trovata (e dunque non è stata precedentemente definita)
  // viene generato un errore
  Function *CalleeF = module->getFunction(Callee);
  if (!CalleeF)
     return LogErrorV("Funzione non definita");
  // Il secondo controllo è che la funzione recuperata abbia tanti parametri
  // quanti sono gi argomenti previsti nel nodo AST
  if (CalleeF->arg_size() != Args.size())
     return LogErrorV("Numero di argomenti non corretto");
  // Passato con successo anche il secondo controllo, viene predisposta
  // ricorsivamente la valutazione degli argomenti presenti nella chiamata 
  // (si ricordi che gli argomenti possono essere espressioni arbitarie)
  // I risultati delle valutazioni degli argomenti (registri SSA, come sempre)
  // vengono inseriti in un vettore, dove "se li aspetta" il metodo CreateCall
  // del builder, che viene chiamato subito dopo per la generazione dell'istruzione
  // IR di chiamata
  std::vector<Value *> ArgsV;
  for (auto arg : Args) {
     ArgsV.push_back(arg->codegen(drv));
     if (!ArgsV.back())
        return nullptr;
  }
  return builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

/************************* If Expression Tree *************************/
IfExprAST::IfExprAST(ExprAST* Cond, ExprAST* TrueExp, ExprAST* FalseExp):
   Cond(Cond), TrueExp(TrueExp), FalseExp(FalseExp) {};

Value* IfExprAST::codegen(driver& drv) {
    // Viene dapprima generato il codice per valutare la condizione, che
    // memorizza il risultato (di tipo i1, dunque booleano) nel registro SSA 
    // che viene "memorizzato" in CondV. 
    Value* CondV = Cond->codegen(drv);
    if (!CondV)
       return nullptr;
    
    // Ora bisogna generare l'istruzione di salto condizionato, ma prima
    // vanno creati i corrispondenti basic block nella funzione attuale
    // (ovvero la funzione di cui fa parte il corrente blocco di inserimento)
    Function *function = builder->GetInsertBlock()->getParent();
    BasicBlock *TrueBB = BasicBlock::Create(*context, "trueexp", function);
    BasicBlock *FalseBB = BasicBlock::Create(*context, "falseexp");
    BasicBlock *MergeBB = BasicBlock::Create(*context, "endcond");

    // Ora possiamo creare l'istruzione di salto condizionato
    builder->CreateCondBr(CondV, TrueBB, FalseBB);
    
    // Generiamo il codice per il blocco true
    builder->SetInsertPoint(TrueBB);
    Value *TrueV = TrueExp->codegen(drv);
    if (!TrueV)
       return nullptr;
    builder->CreateBr(MergeBB);
    TrueBB = builder->GetInsertBlock();
    
    // Generiamo il codice per il blocco false
    function->insert(function->end(), FalseBB);
    builder->SetInsertPoint(FalseBB);
    
    Value *FalseV;
    if (FalseExp) {
        FalseV = FalseExp->codegen(drv);
        if (!FalseV)
            return nullptr;
    } else {
        // Se non c'è il ramo false, usare un valore predefinito
        FalseV = ConstantFP::get(Type::getDoubleTy(*context), 0.0);
    }
    builder->CreateBr(MergeBB);
    FalseBB = builder->GetInsertBlock();
    
    function->insert(function->end(), MergeBB);
    builder->SetInsertPoint(MergeBB);

    // Creiamo il nodo PHI
    PHINode *PN = builder->CreatePHI(TrueV->getType(), 2, "condval");
    PN->addIncoming(TrueV, TrueBB);
    PN->addIncoming(FalseV, FalseBB);
    
    return PN;
};


/********************** Block Expression Tree *********************/
BlockExprAST::BlockExprAST(std::vector<VarBindingAST*> Def, std::vector<ExprAST*> StmtList): 
         Def(std::move(Def)), StmtList(std::move(StmtList)) {};

Value* BlockExprAST::codegen(driver& drv) {
   // Un blocco è un'espressione preceduta dalla definizione di una o più variabili locali.
   // Le definizioni sono opzionali e tuttavia necessarie perché l'uso di un blocco
   // abbia senso. Ad ogni variabile deve essere associato il valore di una costante o il valore di
   // un'espressione. Nell'espressione, arbitraria, possono chiaramente comparire simboli di
   // variabile. Al riguardo, la gestione dello scope (ovvero delle regole di visibilità)
   // è implementata nel modo seguente, in cui, come esempio, consideremo la definzione: var y = x+1
   // 1) Viene dapprima generato il codice per valutare l'espressione x+1.
   //    L'area di memoria da cui "prelevare" il valore di x è scritta in un
   //    registro SSA che è parte della (rappresentazione interna della) istruzione alloca usata
   //    per allocare la memoria corrispondente e che è registrata nella symbol table
   //    Per i parametri della funzione, l'istruzione di allocazione viene generata (come già sappiamo)
   //    dalla chiamata di codegen in FunctionAST. Per le variabili locali viene generata nel presente
   //    contesto. Si noti, di passaggio, che tutte le istruzioni di allocazione verranno poi emesse
   //    nell'entry block, in ordine cronologico rovesciato (rispetto alla generazione). Questo perché
   //    la routine di utilità (CreateEntryBlockAlloca) genera sempre all'inizio del blocco.
   // 2) Ritornando all'esempio, bisogna ora gestire l'assegnamento ad y gestendone la visibilità. 
   //    Come prima cosa viene generata l'istruzione alloca per y. 
   //    Questa deve essere inserita nella symbol table per futuri riferimenti ad y
   //    all'interno del blocco. Tuttavia, se un'istruzione alloca per y fosse già presente nella symbol
   //    table (nel caso y sia un parametro) bisognerebbe "rimuoverla" temporaneamente e re-inserirla
   //    all'uscita del blocco. Questo è ciò che viene fatto dal presente codice, che utilizza
   //    al riguardo il vettore di appoggio "AllocaTmp" (che naturalmente è un vettore di
   //    di (puntatori ad) istruzioni di allocazione
   std::vector<AllocaInst*> AllocaTmp;
   for (int i=0, e=Def.size(); i<e; i++) {
      // Per ogni definizione di variabile si genera il corrispondente codice che
      // (in questo caso) non restituisce un registro SSA ma l'istruzione di allocazione
      AllocaInst *boundval = Def[i]->codegen(drv);
      if (!boundval) 
         return nullptr;
      // Viene temporaneamente rimossa la precedente istruzione di allocazione
      // della stessa variabile (nome) e inserita quella corrente
      AllocaTmp.push_back(drv.NamedValues[Def[i]->getName()]);
      drv.NamedValues[Def[i]->getName()] = boundval;
   };
   // Ora (ed è la parte più "facile" da capire) viene generato il codice che
   // valuta l'espressione. Eventuali riferimenti a variabili vengono risolti
   // nella symbol table appena modificata
   Value *blockvalue = nullptr;
    for (ExprAST* stmt : StmtList) {
      blockvalue = stmt->codegen(drv);
      if (!blockvalue)
          return nullptr;
    }
   // Prima di uscire dal blocco, si ripristina lo scope esterno al costrutto
   for (int i=0, e=Def.size(); i<e; i++) {
        drv.NamedValues[Def[i]->getName()] = AllocaTmp[i];
   };
   // Il valore del costrutto/espressione var è ovviamente il valore (il registro SSA)
   // restituito dal codice di valutazione dell'espressione
   return blockvalue;
};

/************************* Var binding Tree *************************/
VarBindingAST::VarBindingAST(const std::string Name, ExprAST* Val, int ArraySize, std::vector<ExprAST*> ArrayVals)
  : Name(Name), Val(Val), ArraySize(ArraySize), ArrayVals(ArrayVals) {};
   
const std::string& VarBindingAST::getName() const { 
   return Name; 
};

AllocaInst* VarBindingAST::codegen(driver& drv) {
  // Viene subito recuperato il riferimento alla funzione in cui si trova
  // il blocco corrente. Il riferimento è necessario perché lo spazio necessario
  // per memorizzare una variabile (ovunque essa sia definita, si tratti cioè
  // di un parametro oppure di una variabile locale ad un blocco espressione)
  // viene sempre riservato nell'entry block della funzione. Ricordiamo che
  // l'allocazione viene fatta tramite l'utility CreateEntryBlockAlloca
  Function *fun = builder->GetInsertBlock()->getParent();
  AllocaInst *Alloca = nullptr;

  if (ArraySize > 0) {
      llvm::ArrayType *ArrayType = llvm::ArrayType::get(Type::getDoubleTy(*context), ArraySize);
      Alloca = CreateEntryBlockAlloca(fun, Name, ArrayType);
        for (int i = 0; i < ArraySize; ++i) {
            // prendo l'elemento dell'array se esiste, altrimenti metto 0
            Value *ElemVal = (i < ArrayVals.size()) ? ArrayVals[i]->codegen(drv) : ConstantFP::get(*context, APFloat(0.0));
            if (!ElemVal)
                return nullptr;
            Value *ElemPtr = builder->CreateInBoundsGEP(ArrayType, Alloca, ConstantInt::get(*context, APInt(32, i, true)), Name + "_idx_" + std::to_string(i));
            builder->CreateStore(ElemVal, ElemPtr);
        }
  } else {
      Alloca = CreateEntryBlockAlloca(fun, Name);
      Value *BoundVal = Val->codegen(drv);
      if (!BoundVal)
          return nullptr;
      builder->CreateStore(BoundVal, Alloca);
  }

  return Alloca;
};

/************************* Prototype Tree *************************/
PrototypeAST::PrototypeAST(std::string Name, std::vector<std::string> Args):
  Name(Name), Args(std::move(Args)), emitcode(true) {};  //Di regola il codice viene emesso

lexval PrototypeAST::getLexVal() const {
   lexval lval = Name;
   return lval;	
};

const std::vector<std::string>& PrototypeAST::getArgs() const { 
   return Args;
};

// Previene la doppia emissione del codice. Si veda il commento più avanti.
void PrototypeAST::noemit() { 
   emitcode = false; 
};

Function *PrototypeAST::codegen(driver& drv) {
  // Costruisce una struttura, qui chiamata FT, che rappresenta il "tipo" di una
  // funzione. Con ciò si intende a sua volta una coppia composta dal tipo
  // del risultato (valore di ritorno) e da un vettore che contiene il tipo di tutti
  // i parametri. Si ricordi, tuttavia, che nel nostro caso l'unico tipo è double.
  
  // Prima definiamo il vettore (qui chiamato Doubles) con il tipo degli argomenti
  std::vector<Type*> Doubles(Args.size(), Type::getDoubleTy(*context));
  // Quindi definiamo il tipo (FT) della funzione
  FunctionType *FT = FunctionType::get(Type::getDoubleTy(*context), Doubles, false);
  // Infine definiamo una funzione (al momento senza body) del tipo creato e con il nome
  // presente nel nodo AST. ExternalLinkage vuol dire che la funzione può avere
  // visibilità anche al di fuori del modulo
  Function *F = Function::Create(FT, Function::ExternalLinkage, Name, *module);

  // Ad ogni parametro della funzione F (che, è bene ricordare, è la rappresentazione 
  // llvm di una funzione, non è una funzione C++) attribuiamo ora il nome specificato dal
  // programmatore e presente nel nodo AST relativo al prototipo
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  /* Abbiamo completato la creazione del codice del prototipo.
     Il codice può quindi essere emesso, ma solo se esso corrisponde
     ad una dichiarazione extern. Se invece il prototipo fa parte
     della definizione "completa" di una funzione (prototipo+body) allora
     l'emissione viene fatta al momendo dell'emissione della funzione.
     In caso contrario nel codice si avrebbe sia una dichiarazione
     (come nel caso di funzione esterna) sia una definizione della stessa
     funzione.
  */
  if (emitcode) {
    F->print(errs());
    fprintf(stderr, "\n");
  };
  
  return F;
}

/************************* Function Tree **************************/
FunctionAST::FunctionAST(PrototypeAST* Proto, ExprAST* Body): Proto(Proto), Body(Body) {};

Function *FunctionAST::codegen(driver& drv) {
  // Verifica che la funzione non sia già presente nel modulo, cioò che non
  // si tenti una "doppia definizion"
  Function *function = 
      module->getFunction(std::get<std::string>(Proto->getLexVal()));
  // Se la funzione non è già presente, si prova a definirla, innanzitutto
  // generando (ma non emettendo) il codice del prototipo
  if (!function)
    function = Proto->codegen(drv);
  else
    return nullptr;
  // Se, per qualche ragione, la definizione "fallisce" si restituisce nullptr
  if (!function)
    return nullptr;  

  // Altrimenti si crea un blocco di base in cui iniziare a inserire il codice
  BasicBlock *BB = BasicBlock::Create(*context, "entry", function);
  builder->SetInsertPoint(BB);
 
  // Ora viene la parte "più delicata". Per ogni parametro formale della
  // funzione, nella symbol table si registra una coppia in cui la chiave
  // è il nome del parametro mentre il valore è un'istruzione alloca, generata
  // invocando l'utility CreateEntryBlockAlloca già commentata.
  // Vale comunque la pena ricordare: l'istruzione di allocazione riserva 
  // spazio in memoria (nel nostro caso per un double) e scrive l'indirizzo
  // in un registro SSA
  // Il builder crea poi un'istruzione che memorizza il valore del parametro x
  // (al momento contenuto nel registro SSA %x) nell'area di memoria allocata.
  // Si noti che il builder conosce il registro che contiene il puntatore all'area
  // perché esso è parte della rappresentazione C++ dell'istruzione di allocazione
  // (variabile Alloca) 
  
  for (auto &Arg : function->args()) {
    // Genera l'istruzione di allocazione per il parametro corrente
    AllocaInst *Alloca = CreateEntryBlockAlloca(function, Arg.getName());
    // Genera un'istruzione per la memorizzazione del parametro nell'area
    // di memoria allocata
    builder->CreateStore(&Arg, Alloca);
    // Registra gli argomenti nella symbol table per eventuale riferimento futuro
    drv.NamedValues[std::string(Arg.getName())] = Alloca;
  } 
  
  // Ora può essere generato il codice corssipondente al body (che potrà
  // fare riferimento alla symbol table)
  if (Value *RetVal = Body->codegen(drv)) {
    // Se la generazione termina senza errori, ciò che rimane da fare è
    // di generare l'istruzione return, che ("a tempo di esecuzione") prenderà
    // il valore lasciato nel registro RetVal 
    builder->CreateRet(RetVal);

    // Effettua la validazione del codice e un controllo di consistenza
    verifyFunction(*function);
 
    // Emissione del codice su su stderr) 
    function->print(errs());
    fprintf(stderr, "\n");
    return function;
  }

  // Errore nella definizione. La funzione viene rimossa
  function->eraseFromParent();
  return nullptr;
};

/************************* Var Global Tree **************************/
VarGlobalAST::VarGlobalAST(const std::string Name, int ArraySize)
  : Name(Name), ArraySize(ArraySize) {};

Value *VarGlobalAST::codegen(driver& drv) {
  GlobalVariable *gvar = nullptr;
  // Check if the variable is an array
  if (ArraySize > 0) {
      // Create an array type
      llvm::ArrayType *ArrayType = llvm::ArrayType::get(Type::getDoubleTy(*context), ArraySize);
      // Create the global variable for the array
      gvar = new GlobalVariable(*module, ArrayType, false, GlobalValue::CommonLinkage, ConstantAggregateZero::get(ArrayType), Name);
  } else {
      // Create the global variable for a single double
      gvar = new GlobalVariable(*module, Type::getDoubleTy(*context), false, GlobalValue::CommonLinkage, ConstantFP::get(*context, APFloat(0.0)), Name);
  }

  gvar->print(errs());
  fprintf(stderr, "\n");
  return gvar;
};

/************************* Var Assignment Tree **************************/
VarAssignAST::VarAssignAST(const std::string Name, ExprAST *Val, ExprAST *Index) :
    Name(Name), Val(Val), Index(Index) {}

/**
 * Genera codice IR LLVM per l'espressione di assegnazione della variabile.
 *
 * Questa funzione genera codice IR LLVM per l'espressione di assegnazione della variabile.
 * Prende l'oggetto driver come parametro per accedere al contesto LLVM, al modulo,
 * e al builder. Il processo di generazione del codice comporta la creazione di un'istruzione alloca
 * per allocare memoria per la variabile e memorizzare il valore dell'espressione nella
 * memoria allocata. La funzione restituisce il valore LLVM generato.
 *
 * @param drv L'oggetto driver utilizzato per la generazione del codice LLVM.
 * @return Il valore LLVM generato.
 */
Value *VarAssignAST::codegen(driver &drv) {
    // Generate the code for the value to be assigned
    Value *value = Val->codegen(drv);
    if (!value) {
        return LogErrorV("Invalid value for assignment");
    }

    // Check if the assignment is for an array element
    if (Index) {
        // Generate code for the index
        Value *indexValue = Index->codegen(drv);
        if (!indexValue) {
            return LogErrorV("Invalid index for array assignment");
        }

        // Convert the index to an integer
        Value *intIndexValue = builder->CreateFPToSI(indexValue, Type::getInt32Ty(*context), "indexInt");

        // Look up the variable in the symbol table
        AllocaInst *alloca = drv.NamedValues[Name];
        if (!alloca) {
            GlobalVariable *gvar = module->getGlobalVariable(Name);
            if (!gvar) {
                return LogErrorV("Variable not declared: " + Name);
            } else {
                // Create the instruction to store the value in the specific array element
                Value *ElemPtr = builder->CreateInBoundsGEP(gvar->getValueType(), gvar, intIndexValue);
                builder->CreateStore(value, ElemPtr);
            }
        } else {
            // Create the instruction to store the value in the specific array element
            Value *ElemPtr = builder->CreateInBoundsGEP(alloca->getAllocatedType(), alloca, intIndexValue);
            builder->CreateStore(value, ElemPtr);
        }
    } else {
        // Regular variable assignment
        AllocaInst *alloca = drv.NamedValues[Name];
        if (!alloca) {
            GlobalVariable *gvar = module->getGlobalVariable(Name);
            if (!gvar) {
                return LogErrorV("Variable not declared: " + Name);
            } else {
                builder->CreateStore(value, gvar);
            }
        } else {
            builder->CreateStore(value, alloca);
        }
    }

    return value;
}

/************************* For Expression Tree **************************/
ForExprAST::ForExprAST(RootAST* Init, ExprAST* Cond, VarAssignAST* Assign, ExprAST* Stmt)
  : Init(Init), Cond(Cond), Assign(Assign), Stmt(Stmt) {}

/**
 * Genera codice IR LLVM per un'espressione di ciclo for.
 * 
 * Questa funzione genera il codice IR LLVM per un'espressione di ciclo for nel linguaggio Kaleidoscope.
 * Prende l'oggetto driver come parametro e restituisce un valore che rappresenta il risultato del ciclo for.
 * 
 * L'espressione di ciclo for consiste di quattro parti: inizializzazione, condizione, incremento e corpo.
 * L'inizializzazione è un'espressione opzionale che viene eseguita prima dell'inizio del ciclo.
 * La condizione è un'espressione che viene valutata prima di ogni iterazione del ciclo. Se la condizione è falsa, il ciclo termina.
 * L'incremento è un'espressione opzionale che viene eseguita dopo ogni iterazione del ciclo.
 * Il corpo è il blocco di codice che viene eseguito per ogni iterazione del ciclo.
 * 
 * La generazione del codice per un'espressione di ciclo for prevede i seguenti passaggi:
 * 1. Generare il codice per l'espressione di inizializzazione, se esiste.
 * 2. Creare i blocchi base per il ciclo: "loop", "body", "after".
 * 3. Creare un'istruzione di salto al blocco "loop".
 * 4. Impostare il punto di inserimento al blocco "loop" e generare il codice per l'espressione della condizione.
 * 5. Creare un'istruzione di salto condizionale basata sull'espressione della condizione. Se la condizione è falsa, saltare al blocco "after".
 * 6. Impostare il punto di inserimento al blocco "body" e generare il codice per l'espressione del corpo.
 * 7. Generare il codice per l'espressione di incremento, se esiste.
 * 8. Creare un'istruzione di salto incondizionato al blocco "loop".
 * 9. Impostare il punto di inserimento al blocco "after".
 * 10. Restituire 0.0 come risultato del ciclo for.
 * 
 * @param drv L'oggetto driver che fornisce il contesto LLVM, il modulo e il builder.
 * @return Un valore che rappresenta il risultato del ciclo for (0.0 in questo caso).
 */
llvm::Value *ForExprAST::codegen(driver& drv) {
    // Ottiene la funzione corrente
    Function *TheFunction = builder->GetInsertBlock()->getParent();

    // Crea un nuovo blocco base per l'inizializzazione
    BasicBlock *InitBB = BasicBlock::Create(*context, "init",TheFunction);
    builder->CreateBr(InitBB);

    // Crea i blocchi base per il loop
    BasicBlock *CondBB = BasicBlock::Create(*context, "cond", TheFunction);
    BasicBlock *LoopBB = BasicBlock::Create(*context, "loop", TheFunction);
    BasicBlock *AfterBB = BasicBlock::Create(*context, "afterloop", TheFunction);

    // Imposta il punto di inserimento al blocco di inizializzazione
    builder->SetInsertPoint(InitBB);

    //Gestione della variabile in Init
    AllocaInst* AllocaTmp = nullptr;

    auto *Node = dynamic_cast<VarBindingAST*>(Init);
    if(Node) {
      AllocaInst* Val = Node->codegen(drv);

      if(!Val) return nullptr;

      AllocaTmp = drv.NamedValues[Node->getName()];
      drv.NamedValues[Node->getName()] = Val;
    }
    else {
      auto *Node = dynamic_cast<VarAssignAST*>(Init);
      if(!Node) return nullptr;

      Value* Val = Node->codegen(drv);
      if(!Val) return nullptr;
    }

    // Salta alla valutazione della condizione del ciclo
    builder->CreateBr(CondBB);
    builder->SetInsertPoint(CondBB);

     // Genera il codice per la condizione del ciclo
    llvm::Value *CondVal = Cond->codegen(drv);
    if (!CondVal) {
      return LogErrorV("Invalid condition in for loop");
    }

    // Crea il branch condizionale
    builder->CreateCondBr(CondVal, LoopBB, AfterBB);

    // Genera il codice per il corpo del ciclo
    builder->SetInsertPoint(LoopBB);
    llvm::Value *BodyVal = Stmt->codegen(drv);
    if (!BodyVal) {
      return nullptr;
    }

    // Genera il codice per l'assegnazione del ciclo
    if (!Assign->codegen(drv)) {
      return nullptr;
    }

    // Salta di nuovo alla valutazione della condizione
    builder->CreateBr(CondBB);

    // Imposta il punto di inserimento al blocco dopo il ciclo
    builder->SetInsertPoint(AfterBB);

    // Restore della variabile in Init
    if(AllocaTmp) drv.NamedValues[Node->getName()] = AllocaTmp;

    return llvm::Constant::getNullValue(Type::getDoubleTy(*context));
}

/*********************** Boolean Expression Tree ***********************/
BooleanExprAST::BooleanExprAST(char Op, ExprAST* LHS, ExprAST* RHS): 
  Op(Op), LHS(LHS), RHS(RHS) {};

Value* BooleanExprAST::codegen(driver& drv) {
  Value *L = LHS->codegen(drv);
  Value *R = RHS ? RHS->codegen(drv) : nullptr;
  if (!L) return nullptr;

  // Assicurati che L e R siano booleani
  if (L->getType()->isIntegerTy(32))
    L = builder->CreateICmpNE(L, ConstantInt::get(*context, APInt(32, 0, true)), "tmpcmp");
  if (R && R->getType()->isIntegerTy(32))
    R = builder->CreateICmpNE(R, ConstantInt::get(*context, APInt(32, 0, true)), "tmpcmp");

  switch (Op) {
    case 'A':
      return builder->CreateAnd(L, R, "andres");
    case 'O':
      return builder->CreateOr(L, R, "orres");
    case 'N':
      return builder->CreateNot(L, "notres");
    default:
      std::cout << Op << std::endl;
      return LogErrorV("Unsupported boolean operator");
  }
}