// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "source/common/quic/platform/quiche_flags_impl.h"

#include <set>

#include "source/common/common/assert.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"

namespace quiche {

#define DEFINE_QUIC_PROTOCOL_FLAG_SINGLE_VALUE(type, flag, value, doc) type FLAGS_##flag = value;

#define DEFINE_QUIC_PROTOCOL_FLAG_TWO_VALUES(type, flag, internal_value, external_value, doc)      \
  type FLAGS_##flag = external_value;

// Select the right macro based on the number of arguments.
#define GET_6TH_ARG(arg1, arg2, arg3, arg4, arg5, arg6, ...) arg6

#define QUIC_PROTOCOL_FLAG_MACRO_CHOOSER(...)                                                      \
  GET_6TH_ARG(__VA_ARGS__, DEFINE_QUIC_PROTOCOL_FLAG_TWO_VALUES,                                   \
              DEFINE_QUIC_PROTOCOL_FLAG_SINGLE_VALUE)

#define QUIC_PROTOCOL_FLAG(...) QUIC_PROTOCOL_FLAG_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)
#include "quiche/quic/core/quic_protocol_flags_list.h"
#undef QUIC_PROTOCOL_FLAG
#undef QUIC_PROTOCOL_FLAG_MACRO_CHOOSER
#undef GET_6TH_ARG
#undef DEFINE_QUIC_PROTOCOL_FLAG_TWO_VALUES
#undef DEFINE_QUIC_PROTOCOL_FLAG_SINGLE_VALUE

void resetQuicheProtocolFlags(void) {
  ASSERT(Envoy::Thread::MainThread::isMainOrTestThread());
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
