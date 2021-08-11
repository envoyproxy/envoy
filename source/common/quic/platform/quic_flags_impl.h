#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <string>
#include <vector>

#include "source/common/quic/platform/quiche_flags_impl.h"

// Not wired into command-line parsing.
#define DEFINE_QUIC_COMMAND_LINE_FLAG_IMPL(type, flag, value, help)                                \
  quiche::TypedFlag<type>* FLAGS_##flag = new TypedFlag<type>(#flag, value, help);

namespace quic {

// TODO(mpwarres): implement. Lower priority since only used by QUIC command-line tools.
inline std::vector<std::string> QuicParseCommandLineFlagsImpl(const char* /*usage*/, int /*argc*/,
                                                              const char* const* /*argv*/) {
  return std::vector<std::string>();
}

// TODO(mpwarres): implement. Lower priority since only used by QUIC command-line tools.
inline void QuicPrintCommandLineFlagHelpImpl(const char* /*usage*/) {}

} // namespace quic
