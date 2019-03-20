#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy 

#include <string>
#include <vector>

#define GetQuicFlagImpl(flag) 
#define SetQuicFlagImpl(flag, value)
#define GetQuicReloadableFlagImpl(flag)
#define SetQuicReloadableFlagImpl(flag, value)
#define GetQuicRestartFlagImpl(flag)
#define SetQuicRestartFlagImpl(flag, value)

#define DEFINE_QUIC_COMMAND_LINE_FLAG_IMPL(type, name, default_value, help)

namespace quic {
std::vector<std::string> QuicParseCommandLineFlagsImpl(const char* /*usage*/, int /*argc*/,
                                                      const char* const* /*argv*/) {
  return std::vector<std::string>();
}

void QuicPrintCommandLineFlagHelpImpl(const char* /*usage*/) {}

// porting layer for QUICHE.


