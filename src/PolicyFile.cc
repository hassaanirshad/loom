//! @file PolicyFile.cc  Definition of @ref loom::PolicyFile.
/*
 * Copyright (c) 2015-2016 Jonathan Anderson
 * Copyright (c) 2018 Brian Kidney
 * All rights reserved.
 *
 * This software was developed at Memorial University under the
 * NSERC Discovery program (RGPIN-2015-06048).
 *
 * Copyright (c) 2018 Stephen Lee
 * All rights reserved.
 *
 * This software was developed by SRI International, Purdue University,
 * University of Wisconsin and University of Georgia  under DARPA/AFRL
 * contract FA8650-15-C-7562 ("TRACE"), as part of the DARPA Transparent
 * Computing (TC) research program.
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

#include "PolicyFile.hh"
#include "NVSerializer.hh"
#include "Strings.hh"

#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/YAMLTraits.h>

using namespace llvm;
using namespace loom;
using std::string;
using std::unique_ptr;
using std::vector;

namespace {

template <class T> bool vecContains(const std::vector<T> &V, const T &Val) {
  return (std::find(V.begin(), V.end(), Val) != V.end());
}

} // namespace

//
// Data that can be represented in an instrumentation description file:
//

/// A description of how to instrument a function.
struct FnInstrumentation {
  /// Function name (as named by LLVM, possibly language-mangled).
  string Name;

  /// File name (Original source file, Optional)
  string FileName;

  /// Instrumentation that should be applied to calls to this function.
  Policy::Directions Call;

  /// Instrumentation that should be applied to the body of this function
  /// (prologue and epilogue / return points).
  Policy::Directions Body;

  /// Additional information about the function call
  loom::Metadata  Meta;

  /// Additions transformations that should be applied when logging function call.
  vector<loom::Transform> Transforms;
};

/// An operation that can be performed on a variable
enum class Operation
{
  Read,
  Write,
};

/// A description of which operations to instrument on a structure field.
struct FieldInstrumentation {
  /// Field name.
  string Name;

  /// Operations (read/write) that should be instrumented.
  vector<Operation> Operations;
};

/// Serialization strategies we can use (libnv, MessagePack, null...).
enum class SerializationType {
  LibNV,
  None,
};

/// A description of how to instrument fields within a structure.
struct StructInstrumentation {
  /// Name of the structure containing the field (as named by LLVM, possibly
  /// mangled).
  string Name;

  /// Instrumentation that should be applied to calls to this function.
  vector<FieldInstrumentation> Fields;
};

/// A description of how to instrumnt a global variables.
struct GlobalInstrumentation
{
	/// Name of the global variable to instrument
	string Name;

	/// Operations (read/write) that should be instrumented.
	vector<Operation> Operations;
};

/// Everything contained in an instrumentation description file.
struct PolicyFile::PolicyFileData {
  /// Prefix to prepend to all instrumentation hooks (e.g., "__loom").
  string HookPrefix;

  /// How to instrument: inline, via callout function, etc.
  InstrStrategy::Kind Strategy;

  /// Simple (non-serializing) logging strategy.
  SimpleLogger::LogType Logging;

  /// KTrace-based logging.
  Policy::KTraceTarget KTrace;
  
  /// DTrace-based logging.
  Policy::DTraceTarget DTrace;

  /// Serialization.
  SerializationType Serial;

  /// Whether or not to put instrumentation in an explicit block structure.
  bool UseBlockStructure;

  /// Just instrument every instruction.
  bool InstrumentEverything;

  /// Instrument all stores, loads and GEPs.
  bool InstrumentPointerInsts;

  /// Function instrumentation.
  vector<FnInstrumentation> Functions;

  /// Structure field instrumentation.
  vector<StructInstrumentation> Structures;
  
  /// Global variable insstrumentation.
  vector<GlobalInstrumentation> Globals;
};

//
// YAML conversion details:
//

/// Converts a vector to/from YAML.
template <class T> struct yaml::SequenceTraits<vector<T>> {
  static size_t size(yaml::IO &, vector<T> &V) { return V.size(); }
  static T &element(IO &, vector<T> &V, size_t I) {
    if (I >= V.size())
      V.resize(I + 1);

    return V[I];
  }
};

/// Converts an InstrStrategy::Kind to/from YAML.
template <> struct yaml::ScalarEnumerationTraits<InstrStrategy::Kind> {
  static void enumeration(yaml::IO &io, InstrStrategy::Kind &K) {
    io.enumCase(K, "callout", InstrStrategy::Kind::Callout);
    io.enumCase(K, "inline", InstrStrategy::Kind::Inline);
  }
};

/// Converts an KTraceTarget to/from YAML.
template <> struct yaml::ScalarEnumerationTraits<Policy::KTraceTarget> {
  static void enumeration(yaml::IO &io, Policy::KTraceTarget &T) {
    io.enumCase(T, "kernel", Policy::KTraceTarget::Kernel);
    io.enumCase(T, "utrace", Policy::KTraceTarget::Userspace);
    io.enumCase(T, "none", Policy::KTraceTarget::None);
  }
};

/// Converts an DTraceTarget to/from YAML.
template <>
struct yaml::ScalarEnumerationTraits<Policy::DTraceTarget> {
  static void enumeration(yaml::IO &io, Policy::DTraceTarget& T) {
    io.enumCase(T, "userspace", Policy::DTraceTarget::Userspace);
    io.enumCase(T, "none", Policy::DTraceTarget::None);
  }
};

/// Converts a Policy::Direction to/from YAML.
template <> struct yaml::ScalarEnumerationTraits<Policy::Direction> {
  static void enumeration(yaml::IO &io, Policy::Direction &Dir) {
    io.enumCase(Dir, "entry", Policy::Direction::In);
    io.enumCase(Dir, "exit", Policy::Direction::Out);
  }
};

/// Converts a Policy::Metadata to/from YAML.
template <> struct yaml::MappingTraits<loom::Metadata> {
  static void mapping(yaml::IO &io, loom::Metadata &Meta) {
    io.mapOptional("name", Meta.Name);
    io.mapOptional("id", Meta.Id);
  }
};

/// Converts a Policy::Transforms to/from YAML.
template <> struct yaml::MappingTraits<loom::Transform> {
  static void mapping(yaml::IO &io, loom::Transform &Transform) {
    io.mapOptional("arg", Transform.Arg);
    io.mapOptional("fn", Transform.Fn);
  }
};

/// Converts a SerializationType to/from YAML.
template <> struct yaml::ScalarEnumerationTraits<SerializationType> {
  static void enumeration(yaml::IO &io, SerializationType &S) {
    io.enumCase(S, "nv", SerializationType::LibNV);
    io.enumCase(S, "none", SerializationType::None);
  }
};

/// Converts a SimpleLogger::LogType to/from YAML.
template <> struct yaml::ScalarEnumerationTraits<SimpleLogger::LogType> {
  static void enumeration(yaml::IO &io, SimpleLogger::LogType &T) {
    io.enumCase(T, "printf", SimpleLogger::LogType::Printf);
    io.enumCase(T, "xo", SimpleLogger::LogType::Libxo);
    io.enumCase(T, "none", SimpleLogger::LogType::None);
  }
};

/// Converts FnInstrumentation to/from YAML.
template <> struct yaml::MappingTraits<FnInstrumentation> {
  static void mapping(yaml::IO &io, FnInstrumentation &fn) {
    io.mapOptional("name", fn.Name);
    io.mapOptional("within-file", fn.FileName);
    io.mapOptional("caller", fn.Call);
    io.mapOptional("callee", fn.Body);
    io.mapOptional("metadata", fn.Meta);
    io.mapOptional("transforms", fn.Transforms);
  }
};

/// Converts a Operation to/from YAML.
template <>
struct yaml::ScalarEnumerationTraits<Operation> {
  static void enumeration(yaml::IO &io, Operation& Dir) {
    io.enumCase(Dir, "read",  Operation::Read);
    io.enumCase(Dir, "write", Operation::Write);
  }
};

/// Converts FieldInstrumentation to/from YAML.
template <> struct yaml::MappingTraits<FieldInstrumentation> {
  static void mapping(yaml::IO &io, FieldInstrumentation &f) {
    io.mapRequired("name", f.Name);
    io.mapRequired("operations", f.Operations);
  }
};

/// Converts StructInstrumentation to/from YAML.
template <> struct yaml::MappingTraits<StructInstrumentation> {
  static void mapping(yaml::IO &io, StructInstrumentation &s) {
    io.mapRequired("name", s.Name);
    io.mapRequired("fields", s.Fields);
  }
};

///  COnverts GlobalInstrumentation to/from YAML.
template<>
struct yaml::MappingTraits<GlobalInstrumentation> {
	static void  mapping(yaml::IO &io, GlobalInstrumentation &g) {
		io.mapRequired("name",			g.Name);
		io.mapRequired("operations",	g.Operations);
	}
};

/// Converts PolicyFileData to/from YAML.
template <> struct yaml::MappingTraits<PolicyFile::PolicyFileData> {
  static void mapping(yaml::IO &io, PolicyFile::PolicyFileData &policy) {
    io.mapOptional("strategy", policy.Strategy, InstrStrategy::Kind::Callout);
    io.mapOptional("logging", policy.Logging, SimpleLogger::LogType::None);
    io.mapOptional("ktrace", policy.KTrace, Policy::KTraceTarget::None);
    io.mapOptional("dtrace", policy.DTrace, Policy::DTraceTarget::None);
    io.mapOptional("serialization", policy.Serial, SerializationType::None);
    io.mapOptional("block_structure", policy.UseBlockStructure, false);
    io.mapOptional("hook_prefix", policy.HookPrefix, string("__loom"));
    io.mapOptional("everything", policy.InstrumentEverything, false);
    io.mapOptional("pointerInsts", policy.InstrumentPointerInsts, false);
    io.mapOptional("functions", policy.Functions);
    io.mapOptional("structures", policy.Structures);
	io.mapOptional("globals", policy.Globals);
  }
};

//
// PolicyFile implementation:
//

namespace loom {

PolicyFile::PolicyFile(const PolicyFileData &P)
    : Policy(unique_ptr<PolicyFileData>{new PolicyFileData(P)}) {}

PolicyFile::~PolicyFile() {}

ErrorOr<unique_ptr<PolicyFile>> PolicyFile::Open(string Filename) {
  auto F = MemoryBuffer::getFile(Filename);
  if (std::error_code ec = F.getError())
    return ec;

  PolicyFileData policy;
  yaml::Input In(*F.get());
  In >> policy;

  if (std::error_code ec = In.error()) {
    return ec;
  }

  return unique_ptr<PolicyFile>{new PolicyFile(policy)};
}

InstrStrategy::Kind PolicyFile::Strategy() const { return Policy->Strategy; }

SimpleLogger::LogType PolicyFile::Logging() const { return Policy->Logging; }

Policy::KTraceTarget PolicyFile::KTrace() const { return Policy->KTrace; }

Policy::DTraceTarget PolicyFile::DTrace() const { return Policy->DTrace; }

unique_ptr<Serializer> PolicyFile::Serialization(Module& Mod) const
{
  switch (Policy->Serial) {
  case SerializationType::LibNV:
    return unique_ptr<Serializer>(new NVSerializer(Mod));
  case SerializationType::None:
    return unique_ptr<Serializer>(new NullSerializer(Mod.getContext()));
  }
}

bool PolicyFile::UseBlockStructure() const { return Policy->UseBlockStructure; }

bool PolicyFile::InstrumentAll() const { return Policy->InstrumentEverything; }

bool PolicyFile::InstrumentPointerInsts() const {
  return Policy->InstrumentPointerInsts;
}

Policy::Directions PolicyFile::CallHooks(const llvm::Function &Fn) const {
  StringRef Name = Fn.getName();

  for (FnInstrumentation &F : Policy->Functions) {
    if (MatchName(F.Name, Name)) {
      return F.Call;
    }
  }

  return Policy::Directions();
}

Policy::Directions PolicyFile::FnHooks(const llvm::Function &Fn) const {
  StringRef Name = Fn.getName();

  std::string FileName = "";
  DISubprogram* Sp = Fn.getSubprogram();
  if (Sp) {
	  FileName = Sp->getFilename();
  }
  std::string BaseFileName = FileName.substr(FileName.find_last_of("/\\") + 1);

  for (FnInstrumentation &F : Policy->Functions) {
    if (MatchName(F.Name, Name)) {
      if (!F.FileName.empty()) {
        if (MatchName(F.FileName, BaseFileName)) {
          return F.Body;
        }
      } else {
        return F.Body;
      }
    }
  }

  return Policy::Directions();
}

loom::Metadata PolicyFile::InstrMetadata(const llvm::Function &Fn) const {
  StringRef Name = Fn.getName();

  for (FnInstrumentation &F : Policy->Functions) {
    if (MatchName(F.Name, Name)) {
      return F.Meta;
    }
  }
}

vector<loom::Transform> PolicyFile::InstrTransforms(const llvm::Function &Fn) const {
  StringRef Name = Fn.getName();

  for (FnInstrumentation &F : Policy->Functions) {
    if (MatchName(F.Name, Name)) {
      return F.Transforms;
    }
  }
}

bool PolicyFile::StructTypeMatters(const llvm::StructType &T) const {
  if (not T.hasName()) {
    return false;
  }

  if (not T.getName().startswith("struct.")) {
    assert(T.getName().startswith("union."));
    return false;
  }

  // return true;/*
  StringRef Name = T.getName().substr(7);

  for (StructInstrumentation &S : Policy->Structures) {
    if (MatchName(S.Name, Name)) {
      return true;
    }
  }

  return false; // */
}

bool PolicyFile::FieldReadHook(const llvm::StructType &T,
                               StringRef Field) const {
  if (not T.getName().startswith("struct.")) {
    assert(T.getName().startswith("union."));
    return false;
  }

  StringRef Name = T.getName().substr(7);

  for (StructInstrumentation &S : Policy->Structures) {
    if (!MatchName(S.Name, Name)) {
      continue;
    }

    for (auto &F : S.Fields) {
      if (MatchName(F.Name, Field)) {
        return vecContains(F.Operations, Operation::Read);
      }
    }
  }

  return false;
}

bool PolicyFile::FieldWriteHook(const llvm::StructType &T,
                                StringRef Field) const {
  if (not T.getName().startswith("struct.")) {
    assert(T.getName().startswith("union."));
    return false;
  }

  StringRef Name = T.getName().substr(7);

  for (StructInstrumentation &S : Policy->Structures) {
    if (!MatchName(S.Name, Name)) {
      continue;
    }

    for (auto &F : S.Fields) {
      if (MatchName(F.Name, Field)) {
        return vecContains(F.Operations, Operation::Write);
      }
    }
  }

  return false;
}

bool PolicyFile::GlobalValueMatters(const llvm::Value &V) const {
  if (not V.hasName()) {
    return false;
  }

  StringRef Name = V.getName();

  for (GlobalInstrumentation &G : Policy->Globals) {
    if (G.Name == Name) {
      return true;
    }
  }

  return false;
}

bool PolicyFile::GlobalReadHook(const llvm::Value &V) const {
  StringRef Name = V.getName();

  for (GlobalInstrumentation &G : Policy->Globals) {
    if (G.Name != Name) {
      continue;
    }

    return vecContains(G.Operations, Operation::Read);
  }

  return false;
}

bool PolicyFile::GlobalWriteHook(const llvm::Value &V) const {
  StringRef Name = V.getName();

  for (GlobalInstrumentation &G : Policy->Globals) {
    if (G.Name != Name) {
      continue;
    }

    return vecContains(G.Operations, Operation::Write);
  }

  return false;
}

string PolicyFile::InstrName(const vector<string> &Components) const {
  vector<string> FullName(1, Policy->HookPrefix);
  FullName.insert(FullName.end(), Components.begin(), Components.end());

  return Join(FullName, "_");
}

bool PolicyFile::MatchName(std::string instrName, StringRef name) const {

  std::string error; // BJK: What to do with Error?
  llvm::Regex nameRegex = llvm::Regex(instrName);

  if (!nameRegex.isValid(error)) {
    return false;
  }

  if (nameRegex.match(name)) {
    return true;
  }

  return false;
}

} // namespace loom
