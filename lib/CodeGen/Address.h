//===-- Address.h - An aligned address -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class provides a simple wrapper for a pair of a pointer and an
// alignment.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_ADDRESS_H
#define LLVM_CLANG_LIB_CODEGEN_ADDRESS_H

#include "llvm/IR/Constants.h"
#include "clang/AST/CharUnits.h"

namespace clang {
namespace CodeGen {

/// An aligned address.
class Address {
  llvm::Value *Pointer;
  CharUnits Alignment;

private:
  bool _containMMSafePtr;
  llvm::Type *originalPointerTy;
  llvm::Type *rawPointerTy;  // The inner pointer type of a _MMSafe_ptr.

public:
  Address(llvm::Value *pointer, CharUnits alignment)
      : Pointer(pointer), Alignment(alignment) {
    assert((!alignment.isZero() || pointer == nullptr) &&
           "creating valid address with invalid alignment");
    if (pointer) {
      // Backup the original _MMSafe_ptr type.
      originalPointerTy = pointer->getType();

      // Checked C: for _MMSafe_ptr, reset the poitner type.
      if (pointer->getType()->isMMSafePointerTy()) {
        _containMMSafePtr = true;
        rawPointerTy = pointer->getType()->getInnerPtrFromMMSafePtr();
        pointer->mutateType(rawPointerTy);
      }
    }
  }

  // Return true if this Address contains a _MMSafe_ptr.
  bool containMMSafePtr() const { return _containMMSafePtr; }

  //  Set the pointer type to be the inner pointer type of a _MMSafe_ptr.
  void mutatePointerType() {
    if (containMMSafePtr()) Pointer->mutateType(rawPointerTy);
  }

  // Restore the original _MMSafe_ptr type.
  void restoreMMSafePtrType() {
    if (containMMSafePtr()) Pointer->mutateType(originalPointerTy);
  }

  static Address invalid() { return Address(nullptr, CharUnits()); }
  bool isValid() const { return Pointer != nullptr; }

  llvm::Value *getPointer() const {
    assert(isValid());
    return Pointer;
  }

  /// Return the type of the pointer value.
  llvm::PointerType *getType() const {
    llvm::Type *pointerTy = getPointer()->getType();
    if (pointerTy->isMMSafePointerTy()) {
      // Checked C: extract the inner pointer inside an _MMSafe_ptr.
      return pointerTy->getInnerPtrFromMMSafePtr();
    }

    return llvm::cast<llvm::PointerType>(getPointer()->getType());
  }

  /// Return the type of the values stored in this address.
  ///
  /// When IR pointer types lose their element type, we should simply
  /// store it in Address instead for the convenience of writing code.
  llvm::Type *getElementType() const {
    return getType()->getElementType();
  }

  /// Return the address space that this address resides in.
  unsigned getAddressSpace() const {
    return getType()->getAddressSpace();
  }

  /// Return the IR name of the pointer value.
  llvm::StringRef getName() const {
    return getPointer()->getName();
  }

  /// Return the alignment of this pointer.
  CharUnits getAlignment() const {
    assert(isValid());
    return Alignment;
  }
};

/// A specialization of Address that requires the address to be an
/// LLVM Constant.
class ConstantAddress : public Address {
public:
  ConstantAddress(llvm::Constant *pointer, CharUnits alignment)
    : Address(pointer, alignment) {}

  static ConstantAddress invalid() {
    return ConstantAddress(nullptr, CharUnits());
  }

  llvm::Constant *getPointer() const {
    return llvm::cast<llvm::Constant>(Address::getPointer());
  }

  ConstantAddress getBitCast(llvm::Type *ty) const {
    return ConstantAddress(llvm::ConstantExpr::getBitCast(getPointer(), ty),
                           getAlignment());
  }

  ConstantAddress getElementBitCast(llvm::Type *ty) const {
    return getBitCast(ty->getPointerTo(getAddressSpace()));
  }

  static bool isaImpl(Address addr) {
    return llvm::isa<llvm::Constant>(addr.getPointer());
  }
  static ConstantAddress castImpl(Address addr) {
    return ConstantAddress(llvm::cast<llvm::Constant>(addr.getPointer()),
                           addr.getAlignment());
  }
};

}

// Present a minimal LLVM-like casting interface.
template <class U> inline U cast(CodeGen::Address addr) {
  return U::castImpl(addr);
}
template <class U> inline bool isa(CodeGen::Address addr) {
  return U::isaImpl(addr);
}

}

#endif
