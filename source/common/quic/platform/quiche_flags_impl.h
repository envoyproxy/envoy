#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <string>

#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/http/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/synchronization/mutex.h"

namespace quiche {

const std::string EnvoyQuicheReloadableFlagPrefix =
    "envoy.reloadable_features.FLAGS_quic_reloadable_flag_";
const std::string EnvoyQuicheRestartFlagPrefix = "envoy.restart_features.FLAGS_quic_restart_flag_";
const std::string EnvoyFeaturePrefix = "envoy.reloadable_features.";

#define QUIC_PROTOCOL_FLAG(type, flag, ...) extern type FLAGS_##flag;
#include "quiche/quic/core/quic_protocol_flags_list.h"
#undef QUIC_PROTOCOL_FLAG

#define BOOL_STR(b) (b ? "true" : "false")

#define GetQuicheFlagImpl(flag) (quiche::flag)

#define SetQuicheFlagImpl(flag, value) ((quiche::flag) = (value))

#define GetQuicheReloadableFlagImpl(module, flag)                                                  \
  Envoy::Runtime::runtimeFeatureEnabled(                                                           \
      absl::StrCat(quiche::EnvoyQuicheReloadableFlagPrefix, #flag))

#define SetQuicheReloadableFlagImpl(module, flag, value)                                           \
  ASSERT(Envoy::Thread::MainThread::isMainOrTestThread());                                         \
  Envoy::Runtime::LoaderSingleton::getExisting()->mergeValues(                                     \
      {{absl::StrCat(quiche::EnvoyQuicheReloadableFlagPrefix, #flag), BOOL_STR(value)}})

#define GetQuicheRestartFlagImpl(module, flag)                                                     \
  Envoy::Runtime::runtimeFeatureEnabled(absl::StrCat(quiche::EnvoyQuicheRestartFlagPrefix, #flag))

#define SetQuicheRestartFlagImpl(module, flag, value)                                              \
  ASSERT(Envoy::Thread::MainThread::isMainOrTestThread());                                         \
  Envoy::Runtime::LoaderSingleton::getExisting()->mergeValues(                                     \
      {{absl::StrCat(quiche::EnvoyQuicheRestartFlagPrefix, #flag), BOOL_STR(value)}})

static void __attribute__((constructor)) resetQuicheProtocolFlags(void) {
  // Do not include 32-byte per-entry overhead while counting header size.
  SetQuicheFlagImpl(FLAGS_quic_header_size_limit_includes_overhead, false);
  // Set send buffer twice of max flow control window to ensure that stream send
  // buffer always takes all the data.
  // The max amount of data buffered is the per-stream high watermark + the max
  // flow control window of upstream. The per-stream high watermark should be
  // smaller than max flow control window to make sure upper stream can be flow
  // control blocked early enough not to send more than the threshold allows.
  // TODO(#8826) Ideally we should use the negotiated value from upstream which is not accessible
  // for now. 512MB is way to large, but the actual bytes buffered should be bound by the negotiated
  // upstream flow control window.
  SetQuicheFlagImpl(
      FLAGS_quic_buffered_data_threshold,
      2 * ::Envoy::Http2::Utility::OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE); // 512MB
}
} // namespace quiche
