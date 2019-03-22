#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy

#include <string>
#include <vector>

#define GetQuicFlagImpl(flag) []() { return true; }()
#define SetQuicFlagImpl(flag, value)                                                               \
  do {                                                                                             \
  } while (0)
#define GetQuicReloadableFlagImpl(flag) []() { return true; }()
#define SetQuicReloadableFlagImpl(flag, value)                                                     \
  do {                                                                                             \
  } while (0)
#define GetQuicRestartFlagImpl(flag) []() { return true; }()
#define SetQuicRestartFlagImpl(flag, value)                                                        \
  do {                                                                                             \
  } while (0)

#define DEFINE_QUIC_COMMAND_LINE_FLAG_IMPL(type, name, default_value, help)                        \
  do {                                                                                             \
  } while (0)

namespace quic {
std::vector<std::string> QuicParseCommandLineFlagsImpl(const char* /*usage*/, int /*argc*/,
                                                       const char* const* /*argv*/) {
  return std::vector<std::string>();
}

void QuicPrintCommandLineFlagHelpImpl(const char* /*usage*/) {}

// porting layer for QUICHE.
