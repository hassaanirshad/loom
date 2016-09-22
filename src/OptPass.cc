//! @file OptPass.cc  Definition of @ref loom::OptPass.
/*
 * Copyright (c) 2016 Jonathan Anderson
 * Copyright (c) 2016 Cem Kilic
 * All rights reserved.
 *
 * This software was developed by BAE Systems, the University of Cambridge
 * Computer Laboratory, and Memorial University under DARPA/AFRL contract
 * FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent Computing
 * (TC) research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "DebugInfo.hh"
#include "PolicyFile.hh"
#include "Instrumenter.hh"
#include "IRUtils.hh"

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"

#include <unordered_map>

using namespace llvm;
using namespace loom;
using std::string;
using std::unique_ptr;
using std::vector;


namespace {
  /// Name of the YAML-based instrumentation policy file.
  cl::opt<string>
  PolicyFilename("loom-file", cl::desc("instrumentation policy file"),
                 cl::value_desc("filename"), cl::init("loom.policy"));

  struct OptPass : public ModulePass {
    static char ID;
    OptPass() : ModulePass(ID), PolFile(PolicyFile::Open(PolicyFilename)) {}

    bool runOnModule(Module&) override;

    llvm::ErrorOr<unique_ptr<PolicyFile>> PolFile;
  };
}


bool OptPass::runOnModule(Module &Mod)
{
  if (std::error_code err = PolFile.getError()) {
    errs()
      << "Error opening LOOM policy file '" << PolicyFilename
      << "': " << err.message() << "\n";
    return false;
  }

  DebugInfo Debug(Mod);
  if (not Debug.ModuleHasFullDebugInfo()) {
    errs()
      << "Warning: module missing metadata, instrumentation may be incomplete\n"
      ;
  }

  assert(*PolFile);
  Policy& P = **PolFile;

  Instrumenter::NameFn Name =
    [&P](const std::vector<std::string>& Components)
    {
      return P.InstrName(Components);
    };

  unique_ptr<InstrStrategy> S(InstrStrategy::Create(P.Strategy()));
  for (auto& L : P.Loggers(Mod)) {
    S->AddLogger(std::move(L));
  }

  unique_ptr<Instrumenter> Instr(Instrumenter::Create(Mod, Name, std::move(S)));

  //
  // In order to keep from invalidating iterators or instrumenting our
  // instrumentation, we need to decide on the instruction-oriented
  // instrumentation points like calls before we actually instrument them.
  //
  std::unordered_map<Function*, Policy::Directions> Functions;
  std::unordered_map<CallInst*, Policy::Directions> Calls;

  typedef std::pair<GetElementPtrInst*, std::string> FieldGEP;
  std::unordered_map<LoadInst*, FieldGEP> FieldReads;
  std::unordered_map<StoreInst*, FieldGEP> FieldWrites;

  for (auto& Fn : Mod) {
    // Do we need to instrument this function?
    Policy::Directions Directions = P.FnHooks(Fn);
    if (not Directions.empty()) {
      Functions.emplace(&Fn, Directions);
    }

    for (auto& Inst : instructions(Fn)) {
      if (auto *GEP = dyn_cast<GetElementPtrInst>(&Inst)) {
        if (auto *ST = dyn_cast<StructType>(GEP->getSourceElementType())) {
          // A GEP used for structure field lookup should have indices
          // 0 and i, where i is the field number (not byte index).
          if (GEP->getNumIndices() != 2 or !GEP->hasAllConstantIndices())
            continue;

          if (not P.StructTypeMatters(*ST))
            continue;

          std::string FieldName = Debug.FieldName(GEP);
          assert(not FieldName.empty());

          const bool HookReads = P.FieldReadHook(*ST, FieldName);
          const bool HookWrites = P.FieldWriteHook(*ST, FieldName);

          if (not HookReads and not HookWrites) {
            continue;
          }

          for (auto& Use : GEP->uses()) {
            User *U = Use.getUser();

            if (HookReads) {
              if (auto *Load = dyn_cast<LoadInst>(U)) {
                FieldReads[Load] = { GEP, FieldName };
              }
            }

            if (HookWrites) {
              if (auto *Store = dyn_cast<StoreInst>(U)) {
                FieldWrites[Store] = { GEP, FieldName };
              }
            }
          }
        }
      }

      // Is this a call to instrument?
      if (CallInst* Call = dyn_cast<CallInst>(&Inst)) {
        Function *Target = Call->getCalledFunction();
        if (not Target)
          continue; // TODO: support indirect targets

        Policy::Directions Directions = P.CallHooks(*Target);
        if (not Directions.empty())
          Calls.emplace(Call, Directions);
      }
    }
  }

  //
  // Now we actually perform the instrumentation:
  //
  bool ModifiedIR = false;

  for (auto& i : Functions) {
    ModifiedIR |= Instr->Instrument(*i.first, i.second);
  }

  for (auto& i : Calls) {
    ModifiedIR |= Instr->Instrument(i.first, i.second);
  }

  for (auto& i : FieldReads) {
    LoadInst *Load = i.first;
    GetElementPtrInst *GEP = i.second.first;
    StringRef FieldName = i.second.second;

    ModifiedIR |= Instr->Instrument(GEP, Load, FieldName);
  }

  for (auto& i : FieldWrites) {
    StoreInst *Store = i.first;
    GetElementPtrInst *GEP = i.second.first;
    StringRef FieldName = i.second.second;

    ModifiedIR |= Instr->Instrument(GEP, Store, FieldName);
  }

  return ModifiedIR;
}

char OptPass::ID = 0;
static RegisterPass<OptPass> X("loom", "Loom instrumentation", false, false);
