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

#ifndef SVM_FRAMEINFO_H
#define SVM_FRAMEINFO_H

#include "SVM.h"
#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {

class SVMFrameLowering : public TargetFrameLowering {
public:
    SVMFrameLowering();
    void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const;
    void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const;
    bool hasFP(const llvm::MachineFunction&) const;
};

} // llvm namespace

#endif
