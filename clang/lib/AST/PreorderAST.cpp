//===------ PreorderAST.cpp: An n-ary preorder abstract syntax tree -------===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements methods to create and manipulate an n-ary preorder
//  abstract syntax tree which is used to semantically compare two expressions.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/PreorderAST.h"

using namespace clang;

void PreorderAST::AttachNode(Node *N, Node *Parent) {
  // A LeafExprNode cannot be the parent of any node.
  if (Parent && isa<LeafExprNode>(Parent)) {
    assert(0 && "Attempting to add a node to a LeafExprNode");
    SetError();
    return;
  }

  // If the root is null, make the current node the root.
  if (!Root) {
    if (!isa<BinaryOperatorNode>(N)) {
      assert(0 && "The root of a PreorderAST must be a BinaryOperatorNode");
      SetError();
      return;
    }
    if (Parent) {
      assert(0 && "Parent node must be null if the PreorderAST root is null");
      SetError();
      return;
    }
    Root = N;
  }

  // Add the current node to the list of children of its parent.
  if (auto *B = dyn_cast_or_null<BinaryOperatorNode>(Parent))
    B->Children.push_back(N);
  // Set the current node as the child of its parent.
  else if (auto *U = dyn_cast_or_null<UnaryOperatorNode>(Parent))
    U->Child = N;
  // Set the current node as the base of its parent.
  else if (auto *M = dyn_cast_or_null<MemberNode>(Parent))
    M->Base = N;
  // Set the current node as the child of its parent.
  else if (auto *I = dyn_cast_or_null<ImplicitCastNode>(Parent))
    I->Child = N;
}

void PreorderAST::Create(Expr *E, Node *Parent) {
  if (!E)
    return;

  E = Lex.IgnoreValuePreservingOperations(Ctx, E->IgnoreParens());

  if (!Parent) {
    // The invariant is that the root node must be a BinaryOperatorNode with an
    // addition operator. So for expressions like "if (*p)", we don't have a
    // BinaryOperator. So when we enter this function there is no root and the
    // parent is null. So we create a new BinaryOperatorNode with + as the
    // operator and add 0 as a LeafExprNode child of this BinaryOperatorNode.
    // This helps us compare expressions like "p" and "p + 1" by normalizing
    // "p" to "p + 0".

    AddZero(E, Parent);

  } else if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    BinaryOperator::Opcode BinOp = BO->getOpcode();
    Expr *LHS = BO->getLHS();
    Expr *RHS = BO->getRHS();

    // We can convert (e1 - e2) to (e1 + -e2) if -e2 does not overflow.  One
    // instance where -e2 can overflow is if e2 is INT_MIN. Here, instead of
    // specifically checking whether e2 is INT_MIN, we add a unary minus to e2
    // and then check if the resultant expression -e2 overflows. If it
    // overflows, we undo the unary minus operator.

    // TODO: Currently, we can only prove that integer constant expressions do
    // not overflow. We still need to handle proving that non-constant
    // expressions do not overflow.
    if (BO->getOpcode() == BO_Sub &&
        RHS->isIntegerConstantExpr(Ctx)) {
      Expr *UOMinusRHS =
        UnaryOperator::Create(Ctx, RHS, UO_Minus, RHS->getType(),
                              RHS->getValueKind(), RHS->getObjectKind(),
                              SourceLocation(), /*CanOverflow*/ true,
                              FPOptionsOverride());

      SmallVector<PartialDiagnosticAt, 8> Diag;
      UOMinusRHS->EvaluateKnownConstIntCheckOverflow(Ctx, &Diag);

      bool Overflow = false;
      for (auto &PD : Diag) {
        if (PD.second.getDiagID() == diag::note_constexpr_overflow) {
          Overflow = true;
          break;
        }
      }

      if (!Overflow) {
        BinOp = BO_Add;
        RHS = UOMinusRHS;
      }

      // TODO: In case of overflow we leak the memory allocated to UOMinusRHS.
      // Whereas if there is no overflow we leak the memory initially allocated
      // to RHS.
    }

    auto *N = new BinaryOperatorNode(BinOp, Parent);
    AttachNode(N, Parent);

    Create(LHS, /*Parent*/ N);
    Create(RHS, /*Parent*/ N);

  } else if (const auto *ME = dyn_cast<MemberExpr>(E)) {
    Expr *Base = Lex.IgnoreValuePreservingOperations(Ctx, ME->getBase()->IgnoreParens());
    ValueDecl *Field = ME->getMemberDecl();

    // Expressions such as a->f, (*a).f, and a[0].f should have the same
    // canonical form: a MemberNode with a Base node of + [a, 0] and a
    // Field of f. Here, we determine whether the expression E is of one of
    // the forms a->f, (*a).f, etc. and create the base expression a.
    Expr *ArrowBase = nullptr;
    if (ME->isArrow()) {
      ArrowBase = Base;
    } else {
      if (const auto *UO = dyn_cast<UnaryOperator>(Base)) {
        if (UO->getOpcode() == UnaryOperatorKind::UO_Deref)
          ArrowBase = UO->getSubExpr();
      } else if (auto *AE = dyn_cast<ArraySubscriptExpr>(Base)) {
        ArrowBase = BinaryOperator::Create(Ctx, AE->getBase(), AE->getIdx(),
                                           BinaryOperatorKind::BO_Add, AE->getType(),
                                           AE->getValueKind(), AE->getObjectKind(),
                                           AE->getExprLoc(), FPOptionsOverride());
      }
    }

    if (ArrowBase) {
      // If ArrowBase exists, then E is of the form ArrowBase->f,
      // (*ArrowBase).f, etc. The Base of the MemberNode is ArrowBase + 0
      // so that expressions such as a->f, (*a).f, (a + 0)->f, and a[0].f
      // all have the same canonical form.
      auto *N = new MemberNode(Field, /*IsArrow*/ true, Parent);
      AttachNode(N, Parent);
      AddZero(ArrowBase, /*Parent*/ N);
    } else {
      // If no ArrowBase exists, then E is of the form a.f.
      auto *N = new MemberNode(Field, /*IsArrow*/ false, Parent);
      AttachNode(N, Parent);
      Create(Base, /*Parent*/ N);
    }

  } else if (auto *UO = dyn_cast<UnaryOperator>(E)) {
    UnaryOperatorKind Op = UO->getOpcode();
    if (Op == UnaryOperatorKind::UO_Deref) {
      // The child of a dereference operator must be a binary operator so that
      // *e and *(e + 0) have the same canonical form. So for an expression of
      // the form *e, we create a UnaryOperatorNode whose child is a
      // BinaryOperatorNode e + 0.
      auto *Child = UO->getSubExpr();
      auto *N = new UnaryOperatorNode(Op, Parent);
      AttachNode(N, Parent);
      AddZero(Child, /*Parent*/ N);
    } else if ((Op == UnaryOperatorKind::UO_Plus ||
               Op == UnaryOperatorKind::UO_Minus) &&
               E->isIntegerConstantExpr(Ctx)) {
      // For integer constant expressions of the form +e or -e, we create a
      // LeafExprNode rather than a UnaryOperatorNode so that these expressions
      // can be constant folded. Constant folding only folds LeafExprNodes that
      // are children of a BinaryOperatorNode.
      auto *N = new LeafExprNode(E, Parent);
      AttachNode(N, Parent);
    } else {
      auto *N = new UnaryOperatorNode(Op, Parent);
      AttachNode(N, Parent);
      Create(UO->getSubExpr(), /*Parent*/ N);
    }

  } else if (auto *AE = dyn_cast<ArraySubscriptExpr>(E)) {
    // e1[e2] has the same canonical form as *(e1 + e2).
    auto *DerefExpr = BinaryOperator::Create(Ctx, AE->getBase(), AE->getIdx(),
                                             BinaryOperatorKind::BO_Add, AE->getType(),
                                             AE->getValueKind(), AE->getObjectKind(),
                                             AE->getExprLoc(), FPOptionsOverride());
    auto *N = new UnaryOperatorNode(UnaryOperatorKind::UO_Deref, Parent);
    AttachNode(N, Parent);
    // Even though e1 + e2 is already a binary operator, the child of the
    // UnaryOperatorNode should be e1 + e2 + 0. This enables expressions such
    // as p[i + -(1 + 2)] to be constant folded. In order for a
    // BinaryOperatorNode to be constant folded, it must have at least two
    // LeafExprNode children whose expressions are integer constants. For
    // example, i + -(1 + 2) + 0 will be constant folded to i + -3, but
    // i + -(1 + 2) will not be constant folded.
    AddZero(DerefExpr, /*Parent*/ N);

  } else if (auto *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    auto *N = new ImplicitCastNode(ICE->getCastKind(), Parent);
    AttachNode(N, Parent);
    Create(ICE->getSubExpr(), /*Parent*/ N);

  } else {
    auto *N = new LeafExprNode(E, Parent);
    AttachNode(N, Parent);
  }
}

void PreorderAST::AddZero(Expr *E, Node *Parent) {
  auto *N = new BinaryOperatorNode(BO_Add, Parent);
  AttachNode(N, Parent);

  llvm::APInt Zero(Ctx.getTargetInfo().getIntWidth(), 0);
  auto *ZeroLiteral = new (Ctx) IntegerLiteral(Ctx, Zero, Ctx.IntTy,
                                               SourceLocation());
  auto *L = new LeafExprNode(ZeroLiteral, N);
  AttachNode(L, /*Parent*/ N);
  Create(E, /*Parent*/ N);
}

bool BinaryOperatorNode::CanCoalesce() {
  // We can only coalesce if the operator of the current and parent node is
  // commutative and associative. This is because after coalescing we later
  // need to sort the nodes and if the operator is not commutative and
  // associative then sorting would be incorrect.
  if (!IsOpCommutativeAndAssociative())
    return false;
  auto *BParent = dyn_cast_or_null<BinaryOperatorNode>(Parent);
  if (!BParent || !BParent->IsOpCommutativeAndAssociative())
    return false;

  // We can coalesce in the following scenarios:
  // 1. The current and parent nodes have the same operator OR
  // 2. The current node is the only child of its operator node (maybe as a
  // result of constant folding).
  return Opc == BParent->Opc || Children.size() == 1;
}

void BinaryOperatorNode::Coalesce(bool &Changed, bool &Error) {
  if (Error)
    return;

  // Coalesce the children first.
  for (auto *Child : Children)
    Child->Coalesce(Changed, Error);

  if (!CanCoalesce())
    return;

  // If the current node can be coalesced, its parent must be a
  // BinaryOperatorNode.
  auto *BParent = dyn_cast_or_null<BinaryOperatorNode>(Parent);
  if (!BParent)
    return;

  // Remove the current node from the list of children of its parent.
  // Since BParent is modified within the loop, we need to evaluate
  // the loop end on each iteration.
  for (auto I = BParent->Children.begin(); I != BParent->Children.end(); ++I) {
    if (*I == this) {
      BParent->Children.erase(I);
      break;
    }
  }

  // Move all children of the current node to its parent.
  for (auto *Child : Children) {
    Child->Parent = BParent;
    BParent->Children.push_back(Child);
  }

  // Delete the current node.
  delete this;
  Changed = true;
}

void UnaryOperatorNode::Coalesce(bool &Changed, bool &Error) {
  if (Error)
    return;
  Child->Coalesce(Changed, Error);
}

void MemberNode::Coalesce(bool &Changed, bool &Error) {
  if (Error)
    return;
  Base->Coalesce(Changed, Error);
}

void ImplicitCastNode::Coalesce(bool &Changed, bool &Error) {
  if (Error)
    return;
  Child->Coalesce(Changed, Error);
}

void BinaryOperatorNode::Sort(Lexicographic Lex) {
  // Sort the children first.
  for (auto *Child : Children)
    Child->Sort(Lex);

  // We can only sort if the operator is commutative and associative.
  if (!IsOpCommutativeAndAssociative())
    return;

  // Sort the children.
  llvm::sort(Children.begin(), Children.end(),
            [&](Node *N1, Node *N2) {
              return N1->Compare(N2, Lex) == Result::LessThan;
            });
}

void UnaryOperatorNode::Sort(Lexicographic Lex) {
  Child->Sort(Lex);
}

void MemberNode::Sort(Lexicographic Lex) {
  Base->Sort(Lex);
}

void ImplicitCastNode::Sort(Lexicographic Lex) {
  Child->Sort(Lex);
}

void BinaryOperatorNode::ConstantFold(bool &Changed, bool &Error, ASTContext &Ctx) {
  if (Error)
    return;

  size_t ConstStartIdx = 0;
  unsigned NumConsts = 0;
  llvm::APSInt ConstFoldedVal;

  for (size_t I = 0; I != Children.size(); ++I) {
    auto *Child = Children[I];

    // Recursively constant fold the non-leaf children of a BinaryOperatorNode.
    if (!isa<LeafExprNode>(Child)) {
      Child->ConstantFold(Changed, Error, Ctx);
      continue;
    }

    // We can only constant fold if the operator is commutative and
    // associative.
    if (!IsOpCommutativeAndAssociative())
      continue;

    auto *ChildLeafNode = dyn_cast_or_null<LeafExprNode>(Child);
    if (!ChildLeafNode)
      continue;

    // Check if the child node is an integer constant.
    llvm::APSInt CurrConstVal;
    if (!ChildLeafNode->E->isIntegerConstantExpr(CurrConstVal, Ctx))
      continue;

    ++NumConsts;

    if (NumConsts == 1) {
      // We will use ConstStartIdx later in this function to delete the
      // constant folded nodes.
      ConstStartIdx = I;
      ConstFoldedVal = CurrConstVal;

    } else {
      // Constant fold based on the operator.
      bool Overflow;
      switch(Opc) {
        default: continue;
        case BO_Add:
          ConstFoldedVal = ConstFoldedVal.sadd_ov(CurrConstVal, Overflow);
          break;
        case BO_Mul:
          ConstFoldedVal = ConstFoldedVal.smul_ov(CurrConstVal, Overflow);
          break;
      }

      // If we encounter an overflow during constant folding we cannot proceed.
      if (Overflow) {
        Error = true;
        return;
      }
    }
  }

  // To fold constants we need at least 2 constants.
  if (NumConsts <= 1)
    return;

  // Delete the folded constants and reclaim memory.
  // Note: We do not explicitly need to increment the iterator because after
  // erase the iterator automatically points to the new location of the element
  // following the one we just erased.
  llvm::SmallVector<Node *, 2>::iterator I =
    Children.begin() + ConstStartIdx;
  while (NumConsts--) {
    delete(*I);
    Children.erase(I);
  }

  llvm::APInt IntVal(Ctx.getTargetInfo().getIntWidth(),
                     ConstFoldedVal.getLimitedValue());

  Expr *ConstFoldedExpr = new (Ctx) IntegerLiteral(Ctx, IntVal, Ctx.IntTy,
                                                   SourceLocation());

  // Add the constant folded expression to list of children of the current
  // BinaryOperatorNode.
  Children.push_back(new LeafExprNode(ConstFoldedExpr, this));

  // If the constant folded expr is the only child of this BinaryOperatorNode
  // we can coalesce the node.
  if (Children.size() == 1 && CanCoalesce())
    Coalesce(Changed, Error);

  Changed = true;
}

void UnaryOperatorNode::ConstantFold(bool &Changed, bool &Error, ASTContext &Ctx) {
  if (Error)
    return;
  Child->ConstantFold(Changed, Error, Ctx);
}

void MemberNode::ConstantFold(bool &Changed, bool &Error, ASTContext &Ctx) {
  if (Error)
    return;
  Base->ConstantFold(Changed, Error, Ctx);
}

void ImplicitCastNode::ConstantFold(bool &Changed, bool &Error, ASTContext &Ctx) {
  if (Error)
    return;
  Child->ConstantFold(Changed, Error, Ctx);
}

bool PreorderAST::GetDerefOffset(Node *UpperNode, Node *DerefNode,
				 llvm::APSInt &Offset) {
  // Extract the offset by which a pointer is dereferenced. For the pointer we
  // compare the dereference expr with the declared upper bound expr. If the
  // non-integer parts of the two exprs are not equal we say that a valid
  // offset does not exist and return false. If the non-integer parts of the
  // two exprs are equal the offset is calculated as:
  // (integer part of deref expr - integer part of upper bound expr).

  // Since we have already normalized exprs like "*p" to "*(p + 0)" we require
  // that the root of the preorder AST is a BinaryOperatorNode.
  auto *B1 = dyn_cast_or_null<BinaryOperatorNode>(UpperNode);
  auto *B2 = dyn_cast_or_null<BinaryOperatorNode>(DerefNode);

  if (!B1 || !B2)
    return false;

  // If the opcodes mismatch we cannot have a valid offset.
  if (B1->Opc != B2->Opc)
    return false;

  // We have already constant folded the constants. So return false if the
  // number of children mismatch.
  if (B1->Children.size() != B2->Children.size())
    return false;

  // Check if the children are equivalent.
  for (size_t I = 0; I != B1->Children.size(); ++I) {
    auto *Child1 = B1->Children[I];
    auto *Child2 = B2->Children[I];

    if (Child1->Compare(Child2, Lex) == Result::Equal)
      continue;

    // If the children are not equal we require that they be integer constant
    // leaf nodes. Otherwise we cannot have a valid offset.
    auto *L1 = dyn_cast_or_null<LeafExprNode>(Child1);
    auto *L2 = dyn_cast_or_null<LeafExprNode>(Child2);

    if (!L1 || !L2)
      return false;

    // Return false if either of the leaf nodes is not an integer constant.
    llvm::APSInt UpperOffset;
    if (!L1->E->isIntegerConstantExpr(UpperOffset, Ctx))
      return false;

    llvm::APSInt DerefOffset;
    if (!L2->E->isIntegerConstantExpr(DerefOffset, Ctx))
      return false;

    // Offset should always be of the form (ptr + offset). So we check for
    // addition.
    // Note: We have already converted (ptr - offset) to (ptr + -offset). So
    // its okay to only check for addition.
    if (B1->Opc != BO_Add)
      return false;

    // This guards us from a case where the constants were not folded for
    // some reason. In theory this should never happen. But we are adding this
    // check just in case.
    llvm::APSInt Zero(Ctx.getTargetInfo().getIntWidth(), 0);
    if (llvm::APSInt::compareValues(Offset, Zero) != 0)
      return false;

    // offset = deref offset - declared upper bound offset.
    // Return false if we encounter an overflow.
    bool Overflow;
    Offset = DerefOffset.ssub_ov(UpperOffset, Overflow);
    if (Overflow)
      return false;
  }

  return true;
}

Result BinaryOperatorNode::Compare(Node *Other, Lexicographic Lex) {
  Result KindComparison = CompareKinds(Other);
  if (KindComparison != Result::Equal)
    return KindComparison;

  BinaryOperatorNode *B = dyn_cast<BinaryOperatorNode>(Other);
  // If the Opcodes mismatch.
  if (Opc < B->Opc)
    return Result::LessThan;
  if (Opc > B->Opc)
    return Result::GreaterThan;

  size_t ChildCount1 = Children.size(),
         ChildCount2 = B->Children.size();

  // If the number of children of the two nodes mismatch.
  if (ChildCount1 < ChildCount2)
    return Result::LessThan;
  if (ChildCount1 > ChildCount2)
    return Result::GreaterThan;

  // Match each child of the two nodes.
  for (size_t I = 0; I != ChildCount1; ++I) {
    auto *Child1 = Children[I];
    auto *Child2 = B->Children[I];

    Result ChildComparison = Child1->Compare(Child2, Lex);

    // If any child differs between the two nodes.
    if (ChildComparison != Result::Equal)
      return ChildComparison;
  }
  return Result::Equal;
}

Result UnaryOperatorNode::Compare(Node *Other, Lexicographic Lex) {
  Result KindComparison = CompareKinds(Other);
  if (KindComparison != Result::Equal)
    return KindComparison;

  UnaryOperatorNode *U = dyn_cast<UnaryOperatorNode>(Other);

  // If the Opcodes mismatch.
  if (Opc < U->Opc)
    return Result::LessThan;
  if (Opc > U->Opc)
    return Result::GreaterThan;

  return Child->Compare(U->Child, Lex);
}

Result MemberNode::Compare(Node *Other, Lexicographic Lex) {
  Result KindComparison = CompareKinds(Other);
  if (KindComparison != Result::Equal)
    return KindComparison;

  MemberNode *M = dyn_cast<MemberNode>(Other);

  // If the arrow flags mismatch.
  if (IsArrow && !M->IsArrow)
    return Result::LessThan;
  if (!IsArrow && M->IsArrow)
    return Result::GreaterThan;

  // If the fields mismatch.
  Result FieldCompare = Lex.CompareDecl(Field, M->Field);
  if (FieldCompare != Result::Equal)
    return FieldCompare;

  return Base->Compare(M->Base, Lex);
}

Result ImplicitCastNode::Compare(Node *Other, Lexicographic Lex) {
  Result KindComparison = CompareKinds(Other);
  if (KindComparison != Result::Equal)
    return KindComparison;

  ImplicitCastNode *I = dyn_cast<ImplicitCastNode>(Other);

  // If the cast kinds mismatch.
  if (CK < I->CK)
    return Result::LessThan;
  if (CK > I->CK)
    return Result::GreaterThan;

  return Child->Compare(I->Child, Lex);
}

Result LeafExprNode::Compare(Node *Other, Lexicographic Lex) {
  Result KindComparison = CompareKinds(Other);
  if (KindComparison != Result::Equal)
    return KindComparison;

  // Compare the exprs for two leaf nodes.
  LeafExprNode *L = dyn_cast<LeafExprNode>(Other);
  return Lex.CompareExpr(E, L->E);
}

void PreorderAST::Normalize() {
  // TODO: Perform simple arithmetic optimizations/transformations on the
  // constants in the nodes.

  bool Changed = true;
  while (Changed) {
    Changed = false;
    Root->Coalesce(Changed, Error);
    if (Error)
      break;
    Root->Sort(Lex);
    Root->ConstantFold(Changed, Error, Ctx);
    if (Error)
      break;
  }

  if (Ctx.getLangOpts().DumpPreorderAST) {
    PrettyPrint(Root);
    OS << "--------------------------------------\n";
  }
}

void PreorderAST::PrettyPrint(Node *N) {
  if (const auto *B = dyn_cast_or_null<BinaryOperatorNode>(N)) {
    OS << BinaryOperator::getOpcodeStr(B->Opc) << "\n";

    for (auto *Child : B->Children)
      PrettyPrint(Child);
  } else if (const auto *U = dyn_cast_or_null<UnaryOperatorNode>(N)) {
    OS << UnaryOperator::getOpcodeStr(U->Opc) << "\n";
    PrettyPrint(U->Child);
  } else if (const auto *M = dyn_cast_or_null<MemberNode>(N)) {
    if (M->IsArrow)
      OS << "->\n";
    else
      OS << ".\n";
    PrettyPrint(M->Base);
    M->Field->dump(OS);
  } else if (const auto *I = dyn_cast_or_null<ImplicitCastNode>(N)) {
    OS << CastExpr::getCastKindName(I->CK) << "\n";
    PrettyPrint(I->Child);
  } else if (const auto *L = dyn_cast_or_null<LeafExprNode>(N))
    L->E->dump(OS, Ctx);
}

void PreorderAST::Cleanup(Node *N) {
  if (auto *B = dyn_cast_or_null<BinaryOperatorNode>(N))
    for (auto *Child : B->Children)
      Cleanup(Child);

  if (auto *U = dyn_cast_or_null<UnaryOperatorNode>(N))
    Cleanup(U->Child);

  if (auto *M = dyn_cast_or_null<MemberNode>(N))
    Cleanup(M->Base);

  if (auto *I = dyn_cast_or_null<ImplicitCastNode>(N))
    Cleanup(I->Child);

  if (N)
    delete N;
}
