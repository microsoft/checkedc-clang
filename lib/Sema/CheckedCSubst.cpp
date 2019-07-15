//===------ CheckedCInterop - Support Methods for Checked C Interop  -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements methods for doing type substitution and parameter
//  subsitution during semantic analysis.  This is used when typechecking
//  generic type application and checking bounds.
//
//===----------------------------------------------------------------------===//

#include "TreeTransform.h"
#include "clang/AST/TypeVisitor.h"

using namespace clang;
using namespace sema;

ExprResult Sema::ActOnFunctionTypeApplication(ExprResult TypeFunc, SourceLocation Loc,
  ArrayRef<TypeArgument> TypeArgs) {

  TypeFunc = CorrectDelayedTyposInExpr(TypeFunc);
  if (!TypeFunc.isUsable())
    return ExprError();

  // Make sure we have a generic function or function with a bounds-safe
  // interface.

  // TODO: generalize this
  DeclRefExpr *declRef = dyn_cast<DeclRefExpr>(TypeFunc.get());
  if (!declRef) {
    Diag(Loc, diag::err_type_args_limited);
    return ExprError();
  }

  const FunctionProtoType *funcType =
    declRef->getType()->getAs<FunctionProtoType>();

  if (!funcType) {
    Diag(Loc, diag::err_type_args_for_non_generic_expression);
    return ExprError();
  }

  // Make sure that the number of type names equals the number of type variables in 
  // the function type.
  if (funcType->getNumTypeVars() != TypeArgs.size()) {
    FunctionDecl* funDecl = dyn_cast<FunctionDecl>(declRef->getDecl());
    if (!funcType->isGenericFunction() && !funcType->isItypeGenericFunction()) {
      Diag(Loc,
       diag::err_type_args_for_non_generic_expression);
      // TODO: emit a note pointing to the declaration.
      return true;
    }

    // The location of beginning of _For_any is stored in typeVariables
    Diag(Loc,
      diag::err_type_list_and_type_variable_num_mismatch);

    if (funDecl)
      Diag(funDecl->typeVariables()[0]->getBeginLoc(),
        diag::note_type_variables_declared_at);

    return ExprError();
  }
  // Add parsed list of type names to declRefExpr for future references
  declRef->SetGenericInstInfo(Context, TypeArgs);

  // Substitute type arguments for type variables in the function type of the
  // DeclRefExpr.
  QualType NewTy = SubstituteTypeArgs(declRef->getType(), TypeArgs, false /* WithinFieldDecl */);
  declRef->setType(NewTy);
  return declRef;

}

// Type Instantiation

RecordDecl* Sema::ActOnRecordTypeApplication(RecordDecl *Base, ArrayRef<TypeArgument> TypeArgs, bool WithinFieldDecl) {
  assert(Base->isGeneric() && "Base decl must be generic in a type application");
  auto &ctx = Base->getASTContext();

  // Unwrap the type arguments from a 'TypeArgument' to the underlying 'Type *'.
  // TODO(abeln): will the 'ASTContext' free the memory?
  auto RawArgs = new (ctx) llvm::SmallVector<const Type *, 4>();
  for (auto TArg : TypeArgs) {
    // TODO(abeln): remove the 'const_cast'.
    RawArgs->push_back(TArg.typeName.getTypePtr());
  }

  // If possible, just retrieve the application from the cache.
  // This is needed not only for performance, but for correctness to handle
  // recursive references in type applications (e.g. a list which contains a list as a field).
  if (auto Cached = ctx.getCachedTypeApp(Base, *RawArgs)) {
    return Cached;   
  }

  // TODO(abeln): populate the two 'SourceLocation' fields.
  RecordDecl* Inst = RecordDecl::Create(ctx, Base->getTagKind(), Base->getDeclContext(), SourceLocation(), SourceLocation(),
    Base->getIdentifier(), Base->getPreviousDecl(), ArrayRef<TypedefDecl*>(nullptr, (size_t)0) /* TypeParams */, Base, TypeArgs);

  // Mark the decl as complete, even though it doesn't have fields yet.
  // This is because if this method is called as part of creating a 'FieldDecl', then
  // the record type must be marked as complete as soon as the field is defined (so we can't wait
  // until 'CompleteTypeAppFields').
  // Otherwise, we can't type the following example:
  //   struct Box _For_any(T) {};
  //   struct List _For_any(T) { struct Box<T> box; };
  // Notice that the type of 'box' must be complete, since it isn't a pointer.
  // It's ok to mark the decl as complete, since the fields will eventually be populated.
  Inst->setCompleteDefinition();

  // Cache the application early on before we tinker with the fields, in case
  // one of the fields refers back to the application.
  ctx.addCachedTypeApp(Base, *RawArgs, Inst);

  if (WithinFieldDecl) {
    // If we're in a field declaration, we don't want to populate the fields of the instantiated 'RecordDecl'.
    // This is because one of the fields might have a type that recursively refers to the 'Base' record (which we haven't finished parsing).
    // e.g.
    // struct List _For_any(T) {
    //   struct List<T> *next;
    //   T *head;
    // };
    // While processing 'next', we can't instantiate 'List<T>' because we haven't processed the 'head' field yet.
    // The solution is to just return a "dummy" 'RecordDecl' in this case, and "complete it" after we've parsed all the fields.
    Inst->setDelayedTypeApp(true);
    return Inst;
  }

  // If this isn't a field declaration, then we can fill in the fields right away.
  CompleteTypeAppFields(Inst);
  return Inst;
}

void Sema::CompleteTypeAppFields(RecordDecl *Incomplete) {
  assert(Incomplete->isInstantiated() && "Only instantiated record decls can be completed");
  assert(Incomplete->field_empty() && "Can't complete record decl with non-empty fields");

  auto Base = Incomplete->baseDecl();
  for (auto Field : Base->fields()) {
    // Passs WithinFieldDecl = false to force completion of any field types.
    // e.g. suppose we have
    //   struct Box _For_any(T) { T *x; };
    //   struct List _For_any(T) { struct Box<T> box; };
    // While completing the 'box' field of 'List', we want to ensure that the 'x' field of 'Box' is completed
    // in the instantiation of 'Box<T>'.
    QualType InstType = SubstituteTypeArgs(Field->getType(), Incomplete->typeArgs(), false /* WithinFieldDecl */);
    assert(!InstType.isNull() && "Subtitution of type args failed!");
    // TODO(abeln): populate 'SouceLocation' fields.
    // Also make sure that TypeSouceInfo and InstType are in-sync.
    FieldDecl* NewField = FieldDecl::Create(Field->getASTContext(), Incomplete, SourceLocation(), SourceLocation(),
      Field->getIdentifier(), InstType, Field->getTypeSourceInfo(), Field->getBitWidth(), Field->isMutable(), Field->getInClassInitStyle());
    Incomplete->addDecl(NewField);
  }

  Incomplete->setDelayedTypeApp(false);
  // The decl was already marked as complete in 'ActOnRecordTypeApplication'.
}

// Definitions for expanding cycles check
namespace {
  // A graph node is a triple '(BaseRecordDecl, TypeArg, Expanding)' (here stored as two nested 'std::pairs' so we can keep them in a 'DenseSet').
  // The semantics of a triple are as follows: a triple is in the set if, starting from one of the type arguments of 'Base', 
  // it's possible to arrive at 'TypeArg' which is defined in 'BaseRecordDecl'.
  // 'Expanding' indicates whether any of the edges taken to arrive to (BaseRecordDecl, TypeArg) is expanding ('Expanding = 1')
  // or if they're all non-expanding ('Expanding = 0'). 
  using Node = std::pair<std::pair<const RecordDecl *, const TypeVariableType *>, char>;
  const char NON_EXPANDING = 0;
  const char EXPANDING = 1;

  // Retrieve the underlying type variable from a typedef that appears as the param to a generic record.
  // TODO(abeln): handle failure case better.
  const TypeVariableType *GetTypeVar(TypedefDecl *TDef) {
    const TypeVariableType *TypeVar = llvm::dyn_cast<TypeVariableType>(TDef->getUnderlyingType());
    assert(TypeVar && "Expected a type variable as the parameter of a generic record");
    return TypeVar;
  };

  // A 'TypeVisitor' that determines whether a type references a given type variable.
  // e.g. ContainsTypeVar('T').Visit('List<T>') -> true
  //      ContainsTypeVar('T').Visit('List<int>') -> false
  class ContainsTypeVarVisitor : public TypeVisitor<ContainsTypeVarVisitor, bool> {
    // The type variable we're searching for.
    const TypeVariableType *TypeVar;

  public:
    ContainsTypeVarVisitor(const TypeVariableType *TypeVar): TypeVar(TypeVar) {}

    // TODO(abeln): do we need to handle additional cases?

    bool VisitTypeVariableType(const TypeVariableType *Type) {
      return Type == TypeVar;
    }

    bool VisitPointerType(const PointerType *Type) {
      return Visit(Type->getPointeeType().getTypePtr());
    }

    bool VisitElaboratedType(const ElaboratedType *Type) {
      return Visit(Type->getNamedType().getTypePtr());
    }

    bool VisitTypedefType(const TypedefType *Type) {
      return Visit(Type->desugar().getTypePtr());
    }

    bool VisitRecordType(const RecordType *Type) {
      auto RDecl = Type->getDecl();
      if (!RDecl->isInstantiated()) return false;
      for (auto TArg : RDecl->typeArgs()) {
        if (Visit(TArg.typeName.getTypePtr())) return true;
      }
      return false;
    }
  };

  // A 'TypeVisitor' that, given a type and a type variable, generates out-edges from the  
  // type variable in the expanding cycles graph.
  // To generate the edges, we need to destruct the given type and find within it all type
  // applications where the variable appears. The resulting edges are "expanding" or "non-expanding"
  // depending on whether the variable appears at the top level as a type argument.
  //
  // The new edges aren't returned; instead, they're added as a side effect to the
  // 'Worklist' argument in the constructor.
  class ExpandingEdgesVisitor : public TypeVisitor<ExpandingEdgesVisitor, void> {
    // The worklist where the new nodes will be inserted.
    std::stack<Node> &Worklist;
    // The type variable that we're looking for in embedded type applications.
    const TypeVariableType *TypeVar;
    // Whether the path so far contains at least one expanding edge.
    char ExpandingSoFar;
    // A visitor object to find out whether a type variable is referenced within a given type.
    ContainsTypeVarVisitor ContainsVisitor;

  public:
  
    // TODO(abeln): do we need to handle additional cases?

    // Note the worklist argument is mutated by this visitor.
    class ExpandingEdgesVisitor(std::stack<Node>& Worklist, const TypeVariableType *TypeVar, char ExpandingSoFar):
      Worklist(Worklist), TypeVar(TypeVar), ExpandingSoFar(ExpandingSoFar), ContainsVisitor(TypeVar) {}

    void VisitRecordType(const RecordType *Type) {
      auto InstDecl = Type->getDecl();
      if (!InstDecl->isInstantiated()) return;
      auto BaseDecl = InstDecl->baseDecl();
      assert(InstDecl->typeArgs().size() == BaseDecl->typeParams().size() && "Number of type args and params must match");
      auto NumArgs = InstDecl->typeArgs().size();
      for (size_t i = 0; i < NumArgs; i++) {
        auto ArgType = InstDecl->typeArgs()[i].typeName.getCanonicalType().getTypePtr();
        auto DestTypeVar = GetTypeVar(BaseDecl->typeParams()[i]);
        if (ArgType == TypeVar) {
          // Non-expanding edges are created if the type variable appears directly as an argument of the decl.
          // So in this case the new edge is marked as expanding only if we'd previously seen an expanding edge.
          Worklist.push(Node(std::make_pair(BaseDecl, DestTypeVar), ExpandingSoFar));
        } else if (ContainsVisitor.Visit(ArgType)) {
          // Expanding edges are created if the type variable doesn't appear directly, but is contained in the type argument.
          // In this case we always mark the edge as expanding.
          Worklist.push(Node(std::make_pair(BaseDecl, DestTypeVar), true /* expanding */));
        }
      }
    }

    void VisitPointerType(const PointerType *Type) {
      Visit(Type->getPointeeType().getTypePtr());
    }

    void VisitElaboratedType(const ElaboratedType *Type) {
      Visit(Type->getNamedType().getTypePtr());
    }
  };
}

bool Sema::DiagnoseExpandingCycles(RecordDecl *Base, SourceLocation Loc) {
  assert(Base->isGeneric() && "Can only check expanding cycles for generic structs");
  llvm::DenseSet<Node> Visited;
  std::stack<Node> Worklist;

  // 'Base's type variables.
  llvm::SmallVector<const TypeVariableType *, 4> TypeVars; 

  // Seed the worklist with the type parameters to 'Base'.
  for (auto TypeDef : Base->typeParams()) {
    auto TVar = GetTypeVar(TypeDef);
    TypeVars.push_back(TVar);
    Worklist.push(Node(std::make_pair<>(Base, TVar), NON_EXPANDING));
  }

  // Is 'TVar' a type variable of 'Base'?
  auto IsTypeVarOfBase = [&TypeVars](const TypeVariableType *TVar) -> bool {
    for (auto BaseVar : TypeVars) {
      if (BaseVar == TVar) return true;
    }
    return false;
  };

  // Explore the implicit graph via DFS.
  while (!Worklist.empty()) {
    auto Curr = Worklist.top();
    Worklist.pop();
    if (Visited.find(Curr) != Visited.end()) continue; // already visited: don't explore further
    Visited.insert(Curr);
    auto RDecl = Curr.first.first;
    auto TVar = Curr.first.second;
    auto ExpandingSoFar = Curr.second;
    if (ExpandingSoFar == EXPANDING && IsTypeVarOfBase(TVar)) {
      Diag(Loc, diag::err_expanding_cycle);
      return true;
    }
    ExpandingEdgesVisitor EdgesVisitor(Worklist, TVar, ExpandingSoFar);
    for (auto Field : RDecl->fields()) {
      EdgesVisitor.Visit(Field->getType().getTypePtr());
    }
  }

  return false; // no cycles: can complete decls
}

RecordDecl *Sema::GetAsGenericRecordDecl(const Type *Type) {
  while (Type != nullptr) {
    auto AsRec = Type->getAsRecordDecl();
    if (AsRec && AsRec->isInstantiated()) return AsRec;
    // Assume it's a pointer and try again.
    // Use 'getTypePtrOrNull' because 'Type' might in fact not be a pointer.
    Type = Type->getPointeeType().getTypePtrOrNull();
  }
  return nullptr;
}

namespace {
// LocRebuilderTransform is an uncustomized 'TreeTransform' that is used
// solely for re-building 'TypeLocs' within 'TypeApplication'.
// We use this vanilla transform instead of a recursive call to 'TypeApplication::TransformType' because
// we sometimes substitute a type variable for another type variable, and in those cases we
// want to re-build 'TypeLocs', but not do further substitutions.
// e.g.
//   struct Box _For_any(U) { T *x; }
//   struct List _For_any(T) { struct Box<T> box; }
//
// When typing 'Box<T>', we need to substitute 'T' for 'U' in 'Box'.
class LocRebuilderTransform : public TreeTransform<LocRebuilderTransform> {
public:
  LocRebuilderTransform(Sema& SemaRef) : TreeTransform<LocRebuilderTransform>(SemaRef) {}
};

class TypeApplication : public TreeTransform<TypeApplication> {
  typedef TreeTransform<TypeApplication> BaseTransform;
  typedef ArrayRef<TypeArgument> TypeArgList;

private:
  TypeArgList TypeArgs;
  unsigned Depth;
  LocRebuilderTransform LocRebuilder;
  bool WithinFieldDecl;

public:
  TypeApplication(Sema &SemaRef, TypeArgList TypeArgs, unsigned Depth, bool WithinFieldDecl) :
    BaseTransform(SemaRef), TypeArgs(TypeArgs), Depth(Depth), LocRebuilder(SemaRef), WithinFieldDecl(WithinFieldDecl) {}

  QualType TransformTypeVariableType(TypeLocBuilder &TLB,
                                     TypeVariableTypeLoc TL) {
    const TypeVariableType *TV = TL.getTypePtr();
    unsigned TVDepth = TV->GetDepth();

    if (TVDepth < Depth) {
      // Case 1: the type variable is bound by a type quantifier (_Forall scope)
      // that lexically encloses the type quantifier that is being applied.
      // Nothing changes in this case.
      QualType Result = TL.getType();
      TypeVariableTypeLoc NewTL = TLB.push<TypeVariableTypeLoc>(Result);
      NewTL.setNameLoc(TL.getNameLoc());
      return Result;
    } else if (TVDepth == Depth) {
      // Case 2: the type variable is bound by the type quantifier that is
      // being applied.  Substitute the appropriate type argument.
      TypeArgument TypeArg = TypeArgs[TV->GetIndex()];
      TypeLoc NewTL =  TypeArg.sourceInfo->getTypeLoc();
      TLB.reserve(NewTL.getFullDataSize());
      // Run the type transform with the type argument's location information
      // so that the type location class pushed on to the TypeBuilder is the
      // matching class for the transformed type.
      QualType Result = LocRebuilder.TransformType(TLB, NewTL);
      // We don't expect the type argument to change.
      assert(Result == TypeArg.typeName);
      return Result;
    } else {
      // Case 3: the type variable is bound by a type quantifier nested within the
      // the one that is being applied.  Create a type variable with a decremented
      // depth, to account for the removal of the enclosing scope.
      QualType Result =
         SemaRef.Context.getTypeVariableType(TVDepth - 1, TV->GetIndex(),
                                             TV->IsBoundsInterfaceType());
      TypeVariableTypeLoc NewTL = TLB.push<TypeVariableTypeLoc>(Result);
      NewTL.setNameLoc(TL.getNameLoc());
      return Result;
    }
  }

  QualType TransformTypedefType(TypeLocBuilder &TLB, TypedefTypeLoc TL) {
    // Preserve typedef information, unless the underlying type has a type
    // variable embedded in it.
    const TypedefType *T = TL.getTypePtr();
    // See if the underlying type changes.
    QualType UnderlyingType = T->desugar();
    QualType TransformedType = getDerived().TransformType(UnderlyingType);
    if (UnderlyingType == TransformedType) {
      QualType Result = TL.getType();
      // It didn't change, so just copy the original type location information.
      TypedefTypeLoc NewTL = TLB.push<TypedefTypeLoc>(Result);
      NewTL.setNameLoc(TL.getNameLoc());
      return Result;
    }
    // Something changed, so we need to delete the typedef type from the AST and
    // and use the underlying transformed type.

    // Synthesize some dummy type source information.
    TypeSourceInfo *DI = getSema().Context.getTrivialTypeSourceInfo(UnderlyingType,
                                                getDerived().getBaseLocation());
    // Use that to get dummy location information.
    TypeLoc NewTL = DI->getTypeLoc();
    TLB.reserve(NewTL.getFullDataSize());
    // Re-run the type transformation with the dummy location information so
    // that the type location class pushed on to the TypeBuilder is the matching
    // class for the underlying type.
    QualType Result = getDerived().TransformType(TLB, NewTL);
    if (Result != TransformedType) {
      llvm::outs() << "Dumping transformed type:\n";
      Result.dump(llvm::outs());
      llvm::outs() << "Dumping result:\n";
      Result.dump(llvm::outs());
    }
    return Result;
  }

  Decl* TransformDecl(SourceLocation Loc, Decl* D) {
    RecordDecl *RDecl;
    if ((RDecl = dyn_cast<RecordDecl>(D)) && RDecl->isInstantiated()) {
      llvm::SmallVector<TypeArgument, 4> NewArgs;
      ArrayRef<TypeArgument> OrigArgs = RDecl->typeArgs();
      for (auto TArg = OrigArgs.begin(); TArg != OrigArgs.end(); ++TArg) {
        auto NewType = SemaRef.SubstituteTypeArgs(TArg->typeName, TypeArgs, WithinFieldDecl);
        auto *SourceInfo = getSema().Context.getTrivialTypeSourceInfo(NewType, getDerived().getBaseLocation());
        NewArgs.push_back(TypeArgument { NewType, SourceInfo });
      }
      auto *Res = SemaRef.ActOnRecordTypeApplication(RDecl->baseDecl(), ArrayRef<TypeArgument>(NewArgs), WithinFieldDecl);
      return Res;
    } else {
      return BaseTransform::TransformDecl(Loc, D);
    }
  }
};
}

QualType Sema::SubstituteTypeArgs(QualType QT, ArrayRef<TypeArgument> TypeArgs, bool WithinFieldDecl) {
   if (QT.isNull())
     return QT;

   // Transform the type and strip off the quantifier.
   TypeApplication TypeApp(*this, TypeArgs, 0 /* Depth */, WithinFieldDecl);
   QualType TransformedQT = TypeApp.TransformType(QT);

   // Something went wrong in the transformation.
   if (TransformedQT.isNull())
     return QT;

   if (const FunctionProtoType * FPT = TransformedQT->getAs<FunctionProtoType>()) {
     FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
     EPI.GenericFunction = false;
     EPI.ItypeGenericFunction = false;
     EPI.NumTypeVars = 0;
     QualType Result =
       Context.getFunctionType(FPT->getReturnType(), FPT->getParamTypes(), EPI);
     return Result;
   }

   // TODO(abeln): handle types properly (particularly w.r.t caching).
   return TransformedQT;
}
