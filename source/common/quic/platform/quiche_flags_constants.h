#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.
// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "source/common/http/http_option_limits.h"

#include "absl/strings/string_view.h"

#define OVERRIDDEN_RELOADABLE_FLAGS(KEY_VALUE_PAIR)                                                \
  /* Envoy only supports RFC-v1 in the long term, so disable IETF draft 29 implementation by       \
   * default. */                                                                                   \
  KEY_VALUE_PAIR(quic_disable_version_draft_29, true)                                              \
  /* Enable support for HTTP/3 metadata decoding in QUICHE. */                                     \
  KEY_VALUE_PAIR(quic_enable_http3_metadata_decoding, true)                                        \
  /* This flag enables BBR, otherwise QUIC will use Cubic which is less performant */              \
  KEY_VALUE_PAIR(quic_default_to_bbr, true)

#define OVERRIDDEN_PROTOCOL_FLAGS(KEY_VALUE_PAIR)                                                  \
  /* Do not include 32-byte per-entry overhead while counting header size. */                      \
  KEY_VALUE_PAIR(quic_header_size_limit_includes_overhead, false)                                  \
  /* Set send buffer twice of max flow control window to ensure that stream send buffer always     \
   * takes all the data. The max amount of data buffered is the per-stream high watermark + the    \
   * max flow control window of upstream. The per-stream high watermark should be smaller than max \
   * flow control window to make sure upper stream can be flow control blocked early enough not to \
   * send more than the threshold allows. */                                                       \
  /* TODO(#8826) Ideally we should use the negotiated value from upstream which is not accessible  \
   * for now. 512MB is way too large, but the actual bytes buffered should be bound by the         \
   * negotiated upstream flow control window. */                                                   \
  KEY_VALUE_PAIR(quic_buffered_data_threshold,                                                     \
                 2 * ::Envoy::Http2::Utility::OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE)   \
  /* Envoy should send server preferred address without a client option by default. */             \
  KEY_VALUE_PAIR(quic_always_support_server_preferred_address, true)

namespace quiche {

inline constexpr absl::string_view EnvoyQuicheReloadableFlagPrefix =
    "envoy.reloadable_features.FLAGS_envoy_quiche_reloadable_flag_";

inline constexpr absl::string_view EnvoyFeaturePrefix = "envoy.reloadable_features.";

} // namespace quiche
