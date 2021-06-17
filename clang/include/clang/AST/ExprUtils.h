//===----------- ExprUtils.h: Utility functions for expressions ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the utility functions for expressions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_EXPRUTILS_H
#define LLVM_CLANG_EXPRUTILS_H

#include "clang/AST/Expr.h"
#include "clang/Sema/Sema.h"
#include <queue>

namespace clang {

class ExprCreatorUtil {
public:
  // If Op is not a compound operator, CreateBinaryOperator returns a binary
  // operator LHS Op RHS. If Op is a compound operator @=, CreateBinaryOperator
  // returns a binary operator LHS @ RHS. LHS and RHS are cast to rvalues if
  // necessary.
  static BinaryOperator *CreateBinaryOperator(Sema &SemaRef,
                                              Expr *LHS, Expr *RHS,
                                              BinaryOperatorKind Op);

  // Create an unsigned integer literal.
  static IntegerLiteral *CreateUnsignedInt(Sema &SemaRef, unsigned Value);

  // Create an implicit cast expression.
  static ImplicitCastExpr *CreateImplicitCast(Sema &SemaRef, Expr *E,
                                              CastKind CK, QualType T);

  // Create a use of a VarDecl.
  static DeclRefExpr *CreateVarUse(Sema &SemaRef, VarDecl *V);

  // Create a member expression.
  static MemberExpr *CreateMemberExpr(Sema &SemaRef, Expr *Base,
                                      const FieldDecl *Field, bool IsArrow);

  // If e is an rvalue, EnsureRValue returns e. Otherwise, EnsureRValue
  // returns a cast of e to an rvalue, based on the type of e.
  static Expr *EnsureRValue(Sema &SemaRef, Expr *E);

  // Create an integer literal from I. I is interpreted as an unsigned
  // integer.
  static IntegerLiteral *CreateIntegerLiteral(ASTContext &Ctx,
                                              const llvm::APInt &I);

  // If Ty is a pointer type, CreateIntegerLiteral returns an integer literal
  // with a target-dependent bit width. If Ty is an integer type (char,
  // unsigned int, int, etc.), CreateIntegerLiteral returns an integer literal
  // with Ty type.  Otherwise, it returns nullptr.
  static IntegerLiteral *CreateIntegerLiteral(ASTContext &Ctx,
                                              int Value, QualType Ty);

  // Determine if the mathemtical value of I (an unsigned integer) fits within
  // the range of Ty, a signed integer type. APInt requires that bitsizes
  // match exactly, so if I does fit, return an APInt via Result with exactly
  // the bitsize of Ty.
  static bool Fits(ASTContext &Ctx, QualType Ty,
                   const llvm::APInt &I, llvm::APInt &Result);
};

} // end namespace clang

namespace clang {

class VariableUtil {
public:
  // If E is a possibly parenthesized lvalue variable V,
  // GetLValueVariable returns V. Otherwise, it returns nullptr.
  //
  // V may have value-preserving operations applied to it, such as
  // LValueBitCasts.  For example, if E is (LValueBitCast(V)), where V
  // is a variable, GetLValueVariable will return V.
  static DeclRefExpr *GetLValueVariable(Sema &S, Expr *E);

  // If E is a possibly parenthesized rvalue cast of a variable V,
  // GetRValueVariable returns V. Otherwise, it returns nullptr.
  //
  // V may have value-preserving operations applied to it.  For example,
  // if E is (LValueToRValue(LValueBitCast(V))), where V is a variable,
  // GetRValueVariable will return V.
  static DeclRefExpr *GetRValueVariable(Sema &S, Expr *E);

  // IsRValueCastOfVariable returns true if the expression e is a possibly
  // parenthesized lvalue-to-rvalue cast of the lvalue variable v.
  static bool IsRValueCastOfVariable(Sema &S, Expr *E, DeclRefExpr *V);
};

} // end namespace clang

namespace clang {

class ExprUtil {
public:
  // GetRValueCastChild returns the child of a possibly parenthesized
  // rvalue cast.
  static Expr *GetRValueCastChild(Sema &S, Expr *E);

  // ReadsMemoryViaPointer returns true if the expression e reads memory via
  // a pointer.
  // IncludeAllMemberExprs is used to modify the behavior to return true if e
  // is or contains a pointer dereference, member reference, or indirect
  // member reference (including e1.f which may not read memory via a
  // pointer). Returns false if E is nullptr.
  static bool ReadsMemoryViaPointer(Expr *E, bool IncludeAllMemberExprs = false);

  // If LValue appears exactly once in Ei and does not appear in Ej,
  // SplitByLValueCount returns the pair (Ei, Ej).  Otherwise, it returns
  // an empty pair.
  static std::pair<Expr *, Expr *> SplitByLValueCount(Sema &S, Expr *LValue,
                                                      Expr *E1, Expr *E2);

  // LValueOccurrenceCount returns the number of occurrences of the LValue
  // expression in E.
  static unsigned int LValueOccurrenceCount(Sema &S, Expr *LValue, Expr *E);

  // VariableOccurrenceCount returns the number of occurrences of variable
  // expressions in E whose Decls are equivalent to V.
  static unsigned int VariableOccurrenceCount(Sema &S, ValueDecl *V, Expr *E);

  // VariableOccurrenceCount returns the number of occurrences of the Target
  // variable expression in E.
  static unsigned int VariableOccurrenceCount(Sema &S, DeclRefExpr *Target,
                                              Expr *E);
};

} // end namespace clang

namespace clang {
  // QueueSet is a queue backed by a set. The queue is useful for processing
  // the items in a Topological sort order which means that if item1 is a
  // predecessor of item2 then item1 is processed before item2. The set is
  // useful for maintaining uniqueness of items added to the queue.

  template <class T>
  class QueueSet {
  private:
    std::queue<T *> _queue;
    llvm::DenseSet<T *> _set;

  public:
    T *next() const {
      return _queue.front();
    }

    void remove(T *B) {
      if (_queue.empty())
        return;
      _queue.pop();
      _set.erase(B);
    }

    void append(T *B) {
      if (!_set.count(B)) {
        _queue.push(B);
        _set.insert(B);
      }
    }

    bool empty() const {
      return _queue.empty();
    }
  };
} // end namespace clang
#endif
