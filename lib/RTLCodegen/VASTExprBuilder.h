//===----- VASTExprBuilder.h - Building Verilog AST Expressions -*- C++ -*-===//
//
// Copyright: 2011 by SYSU EDA Group. all rights reserved.
// IMPORTANT: This software is supplied to you by Hongbin Zheng in consideration
// of your agreement to the following terms, and your use, installation,
// modification or redistribution of this software constitutes acceptance
// of these terms.  If you do not agree with these terms, please do not use,
// install, modify or redistribute this software. You may not redistribute,
// install copy or modify this software without written permission from
// Hongbin Zheng.
//
//===----------------------------------------------------------------------===//
//
// The Interface to build and optimze VASTExprs.
//
//===----------------------------------------------------------------------===//
#ifndef VTM_VASTEXPR_BUILDER_H
#define VTM_VASTEXPR_BUILDER_H

#include "vtm/VerilogAST.h"

#include "llvm/Support/Allocator.h"

namespace llvm {
struct VASTExprHelper {
  SmallVector<VASTValPtr, 4> Operands;
  VASTExpr::Opcode Opc;
  unsigned BitWidth;
  bool BuildNot;

  void init(VASTExpr::Opcode opc, unsigned bitWidth, bool buildNot = false) {
    Opc = opc;
    BitWidth = bitWidth;
    BuildNot = buildNot;
  }

  void addOperand(VASTValPtr V) {
    Operands.push_back(V.getAsInlineOperand());
  }
};

class VASTExprBuilderContext {
public:
  virtual ~VASTExprBuilderContext() {}

  virtual VASTValPtr nameExpr(VASTValPtr V) { return V; }
  virtual VASTValPtr stripName(VASTValPtr V) const { return V; }

  virtual bool shouldExprBeFlatten(VASTExpr *E) const {
    return E->isInlinable();
  }

  VASTValPtr stripZeroBasedBitSlize(VASTValPtr V) {
    VASTExprPtr Expr = dyn_cast<VASTExpr>(V);
    if (Expr.get() && Expr->isSubBitSlice() && Expr->LB == 0)
      return Expr.getOperand(0);

    return V;
  }

  // If V is an addition which can be flatten the addition that using its result
  // return the expression, or return null otherwise.
  virtual VASTExpr *getAddExprToFlatten(VASTValPtr V) {
    V = stripName(stripZeroBasedBitSlize(V));

    VASTExpr *Expr = dyn_cast<VASTExpr>(V);
    if (!Expr || Expr->getOpcode() != VASTExpr::dpAdd) return 0;

    // We only flatten the expression to make full use of the carry bit.
    // So check if there is only 2 operand and the second operand can be fitted
    // into the carry bit.
    if (Expr->NumOps != 2 || Expr->getOperand(1)->getBitWidth() != 1)
      return 0;

    return Expr;
  }

  virtual VASTImmediate *getOrCreateImmediate(uint64_t Value, int8_t BitWidth) {
    return 0;
  }

  virtual VASTValPtr createExpr(VASTExpr::Opcode Opc, ArrayRef<VASTValPtr> Ops,
                                unsigned UB, unsigned LB) {
    return 0;
  }
};

class VASTExprBuilder {
  void operator=(const VASTExprBuilder &RHS); // DO NOT IMPLEMENT
  VASTExprBuilder(const VASTExprBuilder &RHS); // DO NOT IMPLEMENT

  VASTExprBuilderContext &Context;

  VASTValPtr foldBitSliceExpr(VASTValPtr U, uint8_t UB, uint8_t LB);

  template<VASTExpr::Opcode Opcode, typename visitor>
  void flattenExpr(VASTValPtr V, visitor F);
  template<VASTExpr::Opcode Opcode, typename iterator, typename visitor>
  void flattenExpr(iterator begin, iterator end, visitor F);

  VASTValPtr trimZeros(VASTValPtr V, unsigned &Offset);
  VASTValPtr trimLeadingZeros(VASTValPtr V) {
    unsigned Offset = 0;
    VASTValPtr TrimedVal = trimZeros(V, Offset);
    if (Offset == 0) return TrimedVal;

    return V;
  }

  static bool isAllZeros(VASTValPtr V) {
    if (VASTImmediate *Imm = dyn_cast<VASTImmediate>(V))
      return Imm->isAllZeros();

    return false;
  }

  // Bit mask analyzing, bitmask_collecting_iterator.
  template<VASTExpr::Opcode Opcode, class _Container>
  struct bmc_back_iterator :  public std::back_insert_iterator<_Container> {
    typedef bmc_back_iterator<Opcode, _Container> Self;
    typedef std::back_insert_iterator<_Container> supper;
    typedef VASTExprBuilderContext Context;

    Context &Cntx;
    VASTValPtr MostBenefitBitMask;
    explicit bmc_back_iterator(_Container &C, Context &Cntx)
      : supper(C), MostBenefitBitMask(0), Cntx(Cntx) {}

    Self& operator=(VASTValPtr V) {
      // Do nothing by default.
      supper::operator =(V);
    }
  };

  template<VASTExpr::Opcode Opcode, class _Container>
  bmc_back_iterator<Opcode, _Container> bmc_back_inserter(_Container &C) {
    return bmc_back_iterator<Opcode, _Container>(C, Context);
  }
public:
  explicit VASTExprBuilder(VASTExprBuilderContext &Context)
    : Context(Context) {}

  VASTValPtr getBoolImmediate(bool Val) {
    return Context.getOrCreateImmediate(Val, 1);
  }

  VASTImmediate *getOrCreateImmediate(uint64_t Value, int8_t BitWidth) {
    return Context.getOrCreateImmediate(Value, BitWidth);
  }

  bool shouldExprBeFlatten(VASTExpr *E) const {
    return Context.shouldExprBeFlatten(E);
  }

  VASTValPtr getOrCreateCommutativeExpr(VASTExpr::Opcode Opc,
                                        SmallVectorImpl<VASTValPtr> &Ops,
                                        unsigned BitWidth);

  VASTValPtr buildExpr(VASTExpr::Opcode Opc, ArrayRef<VASTValPtr> Ops,
                       unsigned BitWidth);
  VASTValPtr buildExpr(VASTExpr::Opcode Opc,VASTValPtr Op, unsigned BitWidth);
  VASTValPtr buildExpr(VASTExpr::Opcode Opc, VASTValPtr LHS, VASTValPtr RHS,
                       unsigned BitWidth);
  template<VASTExpr::Opcode Opc>
  static VASTValPtr buildExpr(VASTValPtr LHS, VASTValPtr RHS, unsigned BitWidth,
                              VASTExprBuilder *Builder) {
    return Builder->buildExpr(Opc, LHS, RHS, BitWidth);
  }

  VASTValPtr buildExpr(VASTExpr::Opcode Opc, VASTValPtr Op0, VASTValPtr Op1,
                       VASTValPtr Op2, unsigned BitWidth);
  VASTValPtr buildExpr(VASTExprHelper &Builder) {
    VASTValPtr V = buildExpr(Builder.Opc, Builder.Operands, Builder.BitWidth);

    // If opc is dpAnd and BuildNot is true. It mean Or in And Invert Graph.
    if (Builder.BuildNot) V = buildNotExpr(V);

    return V;
  }

  VASTValPtr buildBitSliceExpr(VASTValPtr U, uint8_t UB, uint8_t LB);
  VASTValPtr buildBitCatExpr(ArrayRef<VASTValPtr> Ops, unsigned BitWidth);
  VASTValPtr buildAndExpr(ArrayRef<VASTValPtr> Ops, unsigned BitWidth);
  VASTValPtr buildMulExpr(ArrayRef<VASTValPtr> Ops, unsigned BitWidth);
  VASTValPtr buildAddExpr(ArrayRef<VASTValPtr> Ops, unsigned BitWidth);

  VASTValPtr buildReduction(VASTExpr::Opcode Opc,VASTValPtr Op,
                            unsigned BitWidth);

  VASTValPtr buildNotExpr(VASTValPtr U);

  static VASTValPtr buildOr(VASTValPtr LHS, VASTValPtr RHS, unsigned BitWidth,
                            VASTExprBuilder *Builder) {
    return Builder->buildOrExpr(LHS, RHS, BitWidth);
  }

  VASTValPtr buildOrExpr(ArrayRef<VASTValPtr> Ops, unsigned BitWidth);

  VASTValPtr buildOrExpr(VASTValPtr LHS, VASTValPtr RHS, unsigned BitWidth) {
    VASTValPtr Ops[] = { LHS, RHS };
    return buildOrExpr(Ops, BitWidth);
  }

  static VASTValPtr buildXor(VASTValPtr LHS, VASTValPtr RHS, unsigned BitWidth,
                             VASTExprBuilder *Builder) {
    VASTValPtr Ops[] = { LHS, RHS };
    return Builder->buildXorExpr(Ops, BitWidth);
  }

  VASTValPtr buildXorExpr(ArrayRef<VASTValPtr> Ops, unsigned BitWidth);
};
}

#endif