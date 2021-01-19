/* -*- mode: C; c-basic-offset: 4; intent-tabs-mode: nil -*-
 *
 * Sifteo VM (SVM) Target for LLVM
 *
 * Micah Elizabeth Scott <micah@misc.name>
 * Copyright <c> 2012 Sifteo, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "Support/ErrorReporter.h"
#include "Analysis/CounterAnalysis.h"
#include "Analysis/UUIDGenerator.h"
#include "Target/SVMSymbolDecoration.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/CommandLine.h"
using namespace llvm;

namespace llvm {
    BasicBlock *createEarlyLTIPass();
}

extern cl::opt<bool> ELFDebug;

namespace {
    class EarlyLTIPass : public BasicBlockPass {
    public:
        static char ID;
        EarlyLTIPass()
            : BasicBlockPass(ID) {}

        virtual bool runOnBasicBlock (BasicBlock &BB);

        void getAnalysisUsage(AnalysisUsage &AU) const {
            AU.addRequired<CounterAnalysis>();
            AU.addRequired<UUIDGenerator>();
        }

        virtual const char *getPassName() const {
            return "Early link-time intrinsics";
        }
        
    private:
        bool runOnCall(CallSite &CS, StringRef Name);
        void replaceWith(CallSite &CS, Value *value);
        void replaceWithConstant(CallSite &CS, uint32_t value);
        void handleGetInitializer(CallSite &CS);
        void handleIsConstant(CallSite &CS);
        Value *getInitializer(Instruction *I, Value *Arg, bool require);
    };
}

char EarlyLTIPass::ID = 0;

BasicBlockPass *llvm::createEarlyLTIPass()
{
    return new EarlyLTIPass();
}

bool EarlyLTIPass::runOnBasicBlock (BasicBlock &BB)
{
    bool Changed = false;

    for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E;) {
        CallSite CS(cast<Value>(I));
        ++I;

        if (CS) {
            Function *F = CS.getCalledFunction();
            if (F && runOnCall(CS, F->getName()))
                Changed = true;
        }
    }

    return Changed;
}

void EarlyLTIPass::replaceWith(CallSite &CS, Value *value)
{
    Instruction *I = CS.getInstruction();
    I->replaceAllUsesWith(value);
    I->eraseFromParent();
}

void EarlyLTIPass::replaceWithConstant(CallSite &CS, uint32_t value)
{
    Instruction *I = CS.getInstruction();
    LLVMContext &Ctx = I->getContext();
    Constant *C = ConstantInt::get(Type::getInt32Ty(Ctx), value);
    replaceWith(CS, ConstantExpr::getIntegerCast(C, I->getType(), false));
}

void EarlyLTIPass::handleGetInitializer(CallSite &CS)
{
    Instruction *I = CS.getInstruction();

    if (CS.arg_size() != 2)
        report_fatal_error(I, "_SYS_lti_initializer requires exactly two args");

    // Parse the 'require' boolean
    ConstantInt *CI = dyn_cast<ConstantInt>(CS.getArgument(1));
    if (!CI)
        report_fatal_error(I, "_SYS_lti_initializer 'require' argument must be a compile-time constant");
    bool require = CI->getZExtValue() != 0;
    
    replaceWith(CS, getInitializer(I, CS.getArgument(0), require));
}

Value *EarlyLTIPass::getInitializer(Instruction *I, Value *Arg, bool require)
{
    Module *M = I->getParent()->getParent()->getParent();

    // The argument likely has casts on it, to take it to void*. Remove those.
    Value *Stripped = Arg->stripPointerCasts();

    // See if we can actually look up the GV initializer
    GlobalVariable *GV = dyn_cast<GlobalVariable>(Stripped);
    if (!GV) {
        if (require)
            report_fatal_error(I, "Argument to _SYS_lti_initializer must be a GlobalVariable");
        else
            return Arg;
    }

    if (!GV->hasInitializer()) {
        if (require)
            report_fatal_error(I, "Argument to _SYS_lti_initializer has no initializer");
        else
            return Arg;
    }
    Constant *Init = GV->getInitializer();

    // The initializer is a raw Constant, and its address can't be taken.
    // We need to create a new GlobalVariable to hold the initializer itself,
    // so that we can return it as a pointer.
    
    Constant *NewGV = new GlobalVariable(*M, Init->getType(),
        true, GlobalValue::PrivateLinkage, Init,
        Twine(SVMDecorations::INIT) + GV->getName(),
        0, false);

    // Cast back to the correct return type
    return ConstantExpr::getPointerCast(NewGV, Arg->getType());
}

void EarlyLTIPass::handleIsConstant(CallSite &CS)
{
    if (CS.arg_size() != 1)
        report_fatal_error(CS.getInstruction(), "_SYS_lti_isConstant requires exactly one arg");

    Constant *C = dyn_cast<Constant>(CS.getArgument(0));
    replaceWithConstant(CS, uint32_t(C != 0));
}

bool EarlyLTIPass::runOnCall(CallSite &CS, StringRef Name)
{
    if (Name == "_SYS_lti_isDebug") {
        replaceWithConstant(CS, ELFDebug);
        return true;
    }

    if (Name == "_SYS_lti_counter") {
        replaceWithConstant(CS, getAnalysis<CounterAnalysis>().getValueFor(CS));
        return true;
    }

    if (Name == "_SYS_lti_uuid") {
        replaceWithConstant(CS, getAnalysis<UUIDGenerator>().getValueFor(CS));
        return true;
    }

    if (Name == "_SYS_lti_initializer") {
        handleGetInitializer(CS);
        return true;
    }

    if (Name == "_SYS_lti_isConstant") {
        handleIsConstant(CS);
        return true;
    }

    return false;
}
