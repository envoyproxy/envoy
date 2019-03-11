#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <vector>

#include "extensions/quic_listeners/quiche/platform/flags_impl.h"

#include "quiche/quic/platform/api/quic_string.h"

#define GetQuicFlagImpl(flag) quiche::FLAGS_##flag.getValue()
#define SetQuicFlagImpl(flag, value) quiche::FLAGS_##flag.SetValue(value)
#define GetQuicReloadableFlagImpl(flag) quiche::FLAGS_quic_reloadable_flag_##flag.getValue()
#define SetQuicReloadableFlagImpl(flag, value)                                                     \
  quiche::FLAGS_quic_reloadable_flag_##flag.SetValue(value)
#define GetQuicRestartFlagImpl(flag) quiche::FLAGS_quic_restart_flag_##flag.getValue()
#define SetQuicRestartFlagImpl(flag, value) quiche::FLAGS_quic_restart_flag_##flag.SetValue(value)

#define DEFINE_QUIC_COMMAND_LINE_FLAG_IMPL(type, name, default_value, help)                        \
  SettableValueArg<type> FLAGS_##flag(/*flag=*/"", /*name=*/#name, /*desc=*/help, /*req=*/false,   \
                                      /*value=*/default_value /*typeDesc=*/#type)

namespace quic {

// TODO(mpwarres): implement. Lower priority since only used by QUIC command-line tools.
std::vector<QuicString> QuicParseCommandLineFlagsImpl(const char* /*usage*/, int /*argc*/,
                                                      const char* const* /*argv*/) {
  return std::vector<QuicString>();
}

// TODO(mpwarres): implement. Lower priority since only used by QUIC command-line tools.
void QuicPrintCommandLineFlagHelpImpl(const char* /*usage*/) {}

} // namespace quic
