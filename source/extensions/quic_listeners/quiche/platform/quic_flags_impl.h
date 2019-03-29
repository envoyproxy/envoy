#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy

#include <string>
#include <vector>

// This is a dummy implementation which just allows its dependency to build.
// TODO(mpwarres): implement once QUICHE flag mechanism is defined.

extern bool FLAGS_quic_supports_tls_handshake;
#define GetQuicFlagImpl(flag) ({ false; })
#define SetQuicFlagImpl(flag, value)                                                               \
  do {                                                                                             \
  } while (0)
#define GetQuicReloadableFlagImpl(flag) ({ false; })
#define SetQuicReloadableFlagImpl(flag, value)                                                     \
  do {                                                                                             \
  } while (0)
#define GetQuicRestartFlagImpl(flag) ({ false; })
#define SetQuicRestartFlagImpl(flag, value)                                                        \
  do {                                                                                             \
  } while (0)

#define DEFINE_QUIC_COMMAND_LINE_FLAG_IMPL(type, name, default_value, help)                        \
  do {                                                                                             \
  } while (0)

namespace quic {
inline std::vector<std::string> QuicParseCommandLineFlagsImpl(const char* /*usage*/, int /*argc*/,
                                                              const char* const* /*argv*/) {
  return std::vector<std::string>();
}

inline void QuicPrintCommandLineFlagHelpImpl(const char* /*usage*/) {}

// porting layer for QUICHE.
