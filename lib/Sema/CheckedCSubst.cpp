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
  QualType NewTy = SubstituteTypeArgs(declRef->getType(), TypeArgs);
  declRef->setType(NewTy);
  return declRef;

}

// Type Instantiation
RecordDecl* Sema::ActOnRecordTypeApplication(RecordDecl *Base, ArrayRef<TypeArgument> TypeArgs) {
  assert(Base->isGeneric() && "Base decl must be generic in a type application");
  assert(Base->getCanonicalDecl() == Base && "Base must be canonical decl");
 
  auto &ctx = Base->getASTContext();

  // Unwrap the type arguments from a 'TypeArgument' to the underlying 'Type *'.
  llvm::SmallVector<const Type *, 4> RawArgs;
  for (auto TArg : TypeArgs) {
    RawArgs.push_back(TArg.typeName.getTypePtr());
  }

  // If possible, just retrieve the application from the cache.
  // This is needed not only for performance, but for correctness to handle
  // recursive references in type applications (e.g. a list which contains a list as a field).
  if (auto Cached = ctx.getCachedTypeApp(Base, RawArgs)) {
    return Cached;   
  }

  // Notice we pass dummy location arguments, since the type application doesn't exist in user code.
  RecordDecl *Inst = RecordDecl::Create(ctx, Base->getTagKind(), Base->getDeclContext(), SourceLocation(), SourceLocation(),
    Base->getIdentifier(), Base->getPreviousDecl(), ArrayRef<TypedefDecl *>(nullptr, static_cast<size_t>(0)) /* TypeParams */, Base, TypeArgs);

  // Add the new instance to the base's context, so that the instance is discoverable
  // by AST traversal operations: e.g. the AST dumper.
  Base->getDeclContext()->addDecl(Inst);

  // Cache the application early on before we tinker with the fields, in case
  // one of the fields refers back to the application.
  ctx.addCachedTypeApp(Base, RawArgs, Inst);

  auto Defn = Base->getDefinition();
  if (Defn && Defn->isCompleteDefinition()) {
    // If the underlying definition exists and has all its fields already populated, then we can complete
    // the type application.
    CompleteTypeAppFields(Inst);
  } else {
    // If the definition isn't available, it might be because we're typing a recursive struct declaration: e.g.
    // struct List _For_any(T) {
    //   struct List<T> *next;
    //   T *head;
    // };
    // While processing 'next', we can't instantiate 'List<T>' because we haven't processed the 'head' field yet.
    // The solution is to mark the application as "delayed" and "complete it" after we've parsed all the fields.
    Inst->setDelayedTypeApp(true);
    ctx.addDelayedTypeApp(Inst);
  }

  return Inst;
}

void Sema::CompleteTypeAppFields(RecordDecl *Incomplete) {
  assert(Incomplete->isInstantiated() && "Only instantiated record decls can be completed");
  assert(Incomplete->field_empty() && "Can't complete record decl with non-empty fields");

  auto Defn = Incomplete->genericBaseDecl()->getDefinition();
  assert(Defn && "The record definition should be populated at this point");
  for (auto *Field : Defn->fields()) {
    QualType InstType = SubstituteTypeArgs(Field->getType(), Incomplete->typeArgs());
    assert(!InstType.isNull() && "Subtitution of type args failed!");
    // TODO: are TypeSouceInfo and InstType in sync?
    FieldDecl *NewField = FieldDecl::Create(Field->getASTContext(), Incomplete, SourceLocation(), SourceLocation(),
      Field->getIdentifier(), InstType, Field->getTypeSourceInfo(), Field->getBitWidth(), Field->isMutable(), Field->getInClassInitStyle());
    Incomplete->addDecl(NewField);
  }

  Incomplete->setDelayedTypeApp(false);
  Incomplete->setCompleteDefinition();
}

namespace {
// LocRebuilderTransform is an uncustomized 'TreeTransform' that is used
// solely for re-building 'TypeLocs' within 'TypeApplication'.
// We use this vanilla transform instead of a recursive call to 'TypeApplication::TransformType' because
// we sometimes substitute a type variable for another type variable, and in those cases we
// want to re-build 'TypeLocs', but not do further substitutions.
// e.g.
//   struct Box _For_any(U) { U *x; }
//   struct List _For_any(T) { struct Box<T> box; }
//
// When typing 'Box<T>', we need to substitute 'T' for 'U' in 'Box'.
// T and U end up with the same representation in the IR because we use an
// index-based representation for variables, not a name-based representation.
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

public:
  TypeApplication(Sema &SemaRef, TypeArgList TypeArgs, unsigned Depth) :
    BaseTransform(SemaRef), TypeArgs(TypeArgs), Depth(Depth), LocRebuilder(SemaRef) {}

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

  Decl *TransformDecl(SourceLocation Loc, Decl *D) {
    RecordDecl *RDecl;
    if ((RDecl = dyn_cast<RecordDecl>(D)) && RDecl->isInstantiated()) {
      llvm::SmallVector<TypeArgument, 4> NewArgs;
      for (auto TArg : RDecl->typeArgs()) {
        auto NewType = SemaRef.SubstituteTypeArgs(TArg.typeName, TypeArgs);
        auto *SourceInfo = getSema().Context.getTrivialTypeSourceInfo(NewType, getDerived().getBaseLocation());
        NewArgs.push_back(TypeArgument { NewType, SourceInfo });
      }
      auto *Res = SemaRef.ActOnRecordTypeApplication(RDecl->genericBaseDecl(), ArrayRef<TypeArgument>(NewArgs));
      return Res;
    } else {
      return BaseTransform::TransformDecl(Loc, D);
    }
  }
};
}

QualType Sema::SubstituteTypeArgs(QualType QT, ArrayRef<TypeArgument> TypeArgs) {
   if (QT.isNull())
     return QT;

   // Transform the type and strip off the quantifier.
   TypeApplication TypeApp(*this, TypeArgs, 0 /* Depth */);
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

   return TransformedQT;
}
