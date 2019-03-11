#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "tclap/CmdLineInterface.h"
#include "tclap/ValueArg.h"

namespace quiche {

// Subclass supporting explicit setting of flag value.
template <typename T> class SettableValueArg : public TCLAP::ValueArg<T> {
public:
  using TCLAP::ValueArg<T>::ValueArg;
  void SetValue(const T& value) { this->_value = value; }
};

// Flag declarations
#define QUICHE_FLAG(type, flag, value, doc) extern SettableValueArg<type> FLAGS_##flag;
#include "extensions/quic_listeners/quiche/platform/flags_list.h"
#undef QUICHE_FLAG

// Register flags to be parsed by TCLAP command-line parser.
void RegisterFlags(TCLAP::CmdLineInterface& cmdline);

// Resets all flags to default values.
void ResetFlags();

}  // namespace quiche
