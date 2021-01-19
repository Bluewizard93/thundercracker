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

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Attributes.h"
using namespace llvm;

namespace llvm {
    ModulePass *createInlineGlobalCtorsPass();
}

namespace {
    class InlineGlobalCtorsPass : public ModulePass {
    public:
        static char ID;
        InlineGlobalCtorsPass()
            : ModulePass(ID) {}
        
        virtual bool runOnModule(Module &M);

        virtual llvm::StringRef getPassName() const {
            return "Inlining global constructors";
        }
        
    private:
        void createStubAtExit(Module &M);
        void removeCtorDtorLists(Module &M);
        void inlineCtorList(Module &M);
    };
}

char InlineGlobalCtorsPass::ID = 0;


bool InlineGlobalCtorsPass::runOnModule(Module &M)
{
    createStubAtExit(M);
    inlineCtorList(M);
    removeCtorDtorLists(M);

    return true;
}

void InlineGlobalCtorsPass::inlineCtorList(Module &M)
{
    // All constructor pointers are in a global constant array
    GlobalVariable *GV = M.getGlobalVariable("llvm.global_ctors");
    if (!GV)
        return;
    assert(GV->hasUniqueInitializer());
    if (isa<ConstantAggregateZero>(GV->getInitializer()))
        return;

    // Operate on a new basic block, prepended to main()
    Function *Main = M.getFunction("main");
    if (!Main)
        return;
    LLVMContext &Ctx = M.getContext();
    BasicBlock *EntryBB = &Main->getEntryBlock();
    BasicBlock *CtorBB = BasicBlock::Create(Ctx, "CtorBlock", Main, EntryBB);

    // Add each constructor, in order.
    ConstantArray *CA = cast<ConstantArray>(GV->getInitializer());
    for (User::op_iterator i = CA->op_begin(), e = CA->op_end(); i != e; ++i) {
        ConstantStruct *CS = cast<ConstantStruct>(*i);
        Function *F = dyn_cast<Function>(CS->getOperand(1));
        if (F)
            CallInst::Create(F, "", CtorBB);
    }

    // Branch to the real main()
    BranchInst::Create(EntryBB, CtorBB);
}

void InlineGlobalCtorsPass::createStubAtExit(Module &M)
{
    /*
     * If the module has declared a _cxa_atexit function, give it a stub
     * body. We have no need to run static destructors, so stubbing out
     * this function will effectively remove such destructors from the
     * program after another round of inlining.
     */
    
    if (Function *F = M.getFunction("__cxa_atexit")) {
        LLVMContext &Ctx = M.getContext();

        // return 0;
        BasicBlock *BB = BasicBlock::Create(Ctx, "EntryBlock", F);
        Value *Zero = ConstantInt::get(Type::getInt32Ty(Ctx), 0);
        ReturnInst::Create(Ctx, Zero, BB);

        // Always expand inline, and don't include this function in the image
        F->addFnAttr(Attribute::AlwaysInline);
        F->setLinkage(GlobalValue::InternalLinkage);
    }
}

void InlineGlobalCtorsPass::removeCtorDtorLists(Module &M)
{
    /*
     * After we've finished with the global constructor/destructor lists,
     * remove them from the module. They are now redundant with the inlined
     * calls.
     */

     if (GlobalVariable *GV = M.getGlobalVariable("llvm.global_ctors"))
         GV->eraseFromParent();
     if (GlobalVariable *GV = M.getGlobalVariable("llvm.global_dtors"))
         GV->eraseFromParent();
}

ModulePass *llvm::createInlineGlobalCtorsPass()
{
    return new InlineGlobalCtorsPass();
}
