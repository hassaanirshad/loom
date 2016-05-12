//! @file Logger.cc  Definition of @ref loom::Logger.
/*
 * Copyright (c) 2016 Jonathan Anderson
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

#include "Logger.hh"

#include <llvm/IR/Module.h>
#include <llvm/IR/TypeBuilder.h>

#include <sstream>

using namespace llvm;
using namespace loom;
using std::string;
using std::unique_ptr;
using std::vector;


namespace {
  class LibxoLogger : public Logger {
  public:
    LibxoLogger(Module& Mod) : Logger(Mod) {}

    StringRef FunctionName() const override { return "xo_emit"; }
    Value* CreateFormatString(IRBuilder<>&, StringRef Prefix,
                              ArrayRef<Parameter> Params,
                              StringRef Suffix) override;
  };

  class PrintfLogger : public Logger {
  public:
    PrintfLogger(Module& Mod) : Logger(Mod) {}

    StringRef FunctionName() const override { return "printf"; }
    Value* CreateFormatString(IRBuilder<>&, StringRef Prefix,
                              ArrayRef<Parameter> Params,
                              StringRef Suffix) override;
  };
}


unique_ptr<Logger> Logger::Create(Module& Mod, LogType Log) {
  switch (Log) {
  case LogType::Printf:
    return unique_ptr<Logger>(new PrintfLogger(Mod));

  case LogType::Libxo:
    return unique_ptr<Logger>(new LibxoLogger(Mod));

  case LogType::None:
    return unique_ptr<Logger>();
  }
}


CallInst* Logger::Call(IRBuilder<>& Builder, StringRef Prefix,
                       ArrayRef<Value*> Values, StringRef Suffix) {

  vector<Value*> Args = AdaptArguments(Values, Builder);

  Value *FormatString = CreateFormatString(Builder, Prefix, Args, Suffix);
  Args.emplace(Args.begin(), FormatString);

  return Builder.CreateCall(GetFunction(), Args);
}


CallInst* Logger::Call(IRBuilder<>& Builder, StringRef FormatStringPrefix,
                       Function::ArgumentListType& Args, StringRef Suffix) {

  vector<Value*> LogArgs(Args.size());
  std::transform(Args.begin(), Args.end(), LogArgs.begin(),
                 [&](Value& V) { return &V; });

  return Call(Builder, FormatStringPrefix, LogArgs, Suffix);
}


Function* Logger::GetFunction() {
  const string Name = FunctionName();

  Function *Fn = Mod.getFunction(Name);
  if (Fn)
    return Fn;

  return Function::Create(GetType(), Function::ExternalLinkage, Name, &Mod);
}


FunctionType* Logger::GetType() {
  return TypeBuilder<int(const char *, ...), false>::get(Mod.getContext());
}


Value* Logger::CreateFormatString(IRBuilder<>& Builder, StringRef Prefix,
                                  ArrayRef<Value*> Values, StringRef Suffix) {
  ParamVec NamedTypes;
  for (Value *V : Values) {
    NamedTypes.emplace_back(V->getName(), V->getType());
  }

  return CreateFormatString(Builder, Prefix, NamedTypes, Suffix);
}


vector<Value*> Logger::AdaptArguments(ArrayRef<Value*> Values,
                                      IRBuilder<>& Builder) {
  vector<Value*> Adapted;

  for (Value *V : Values) {
    if (V->getType()->isFloatTy()) {
      V = Builder.CreateFPExt(V, Builder.getDoubleTy());
    }

    Adapted.push_back(V);
  }

  return Adapted;
}


Value*
LibxoLogger::CreateFormatString(IRBuilder<>& Builder, StringRef Prefix,
                                ArrayRef<Parameter> Params, StringRef Suffix) {

  std::stringstream FormatString;

  FormatString << Prefix.str();

  for (auto& P : Params) {
    const string Name = P.first;
    Type *T = P.second;

    FormatString
      << "{P: }"                 // padding
      << "{h:" << Name << "/"    // humanize values (e.g., "44M")
      ;

    if (T->isIntegerTy(32)) {
      FormatString << "%d";

    } else if (T->isFloatTy() || T->isDoubleTy()) {
      FormatString << "%.0f";

    } else if (T->isIntegerTy(8)) {
      FormatString << "%c";

    } else if (T->isPointerTy()
               and T->getPointerElementType()->isIntegerTy(8)) {
      FormatString << "\"%s\"";

    } else if (T->isPointerTy()) {
      FormatString << "%p";

    }

    FormatString << "}"; // close the format string
  }

  FormatString << Suffix.str();

  return Builder.CreateGlobalStringPtr(FormatString.str());
}


Value*
PrintfLogger::CreateFormatString(IRBuilder<>& Builder, StringRef Prefix,
                                 ArrayRef<Parameter> Params, StringRef Suffix) {

  std::stringstream FormatString;

  FormatString << Prefix.str();

  for (auto& P : Params) {
    Type *T = P.second;

    if (T->isIntegerTy(32)) {
      FormatString << " %d";

    } else if (T->isFloatTy() || T->isDoubleTy()) {
      FormatString << " %.0f";

    } else if (T->isIntegerTy(8)) {
      FormatString << " %c";

    } else if (T->isPointerTy()
               and T->getPointerElementType()->isIntegerTy(8)) {
      FormatString << " \"%s\"";

    } else if (T->isPointerTy()) {
      FormatString << " %p";

    }
  }

  FormatString << Suffix.str();

  return Builder.CreateGlobalStringPtr(FormatString.str());
}
