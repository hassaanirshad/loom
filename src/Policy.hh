//! @file Policy.hh  Definition of @ref loom::Policy.
/*
 * Copyright (c) 2015-2016 Jonathan Anderson
 * All rights reserved.
 *
 * This software was developed at Memorial University under the
 * NSERC Discovery program (RGPIN-2015-06048).
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

#ifndef LOOM_POLICY_H
#define LOOM_POLICY_H

#include "InstrStrategy.hh"

#include <string>
#include <vector>


namespace llvm
{
  class Function;
  class StructType;
}


namespace loom {

/**
 * An instrumentation policy tells us what things to instrument and in
 * which direction (e.g., call vs return).
 */
class Policy
{
  public:
  virtual InstrStrategy::Kind Strategy() const = 0;

  //! Simple (non-serializing) logging.
  virtual SimpleLogger::LogType Logging() const = 0;

  enum class Direction { In, Out };
  typedef std::vector<Direction> Directions;

  //! In which directions should calls to a function be instrumented?
  virtual Directions CallHooks(const llvm::Function&) const = 0;

  //! In which directions (preamble/return) should a function be instrumented?
  virtual Directions FnHooks(const llvm::Function&) const = 0;

  /**
   * Should a read from a structure field be instrumented?
   *
   * @param   T     the type of the structure being read from
   * @param   F     the field being read from within the structure
   */
  virtual bool
    FieldReadHook(const llvm::StructType& T, unsigned int F) const = 0;

  /**
   * Should a write to a structure field be instrumented?
   *
   * @param   T     the type of the structure being written to
   * @param   F     the field being written to within the structure
   */
  virtual bool
    FieldWriteHook(const llvm::StructType& T, unsigned int F) const = 0;

  //! Name an instrumentation function for a particular event type.
  virtual std::string
    InstrName(const std::vector<std::string>& Components) const = 0;
};

} // namespace loom

#endif /* LOOM_POLICY_H */
