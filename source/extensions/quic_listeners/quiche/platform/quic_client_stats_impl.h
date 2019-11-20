#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <string>

// NOTE(wub): These macros are currently NOOP because they are supposed to be
// used by client-side stats. They should be implemented when QUIC client code
// is used by Envoy to connect to backends.

#define QUIC_CLIENT_HISTOGRAM_ENUM_IMPL(name, sample, enum_size, docstring)                        \
  do {                                                                                             \
    (void)(sample);                                                                                \
  } while (0)
#define QUIC_CLIENT_HISTOGRAM_BOOL_IMPL(name, sample, docstring)                                   \
  (void)(sample);                                                                                  \
  do {                                                                                             \
  } while (0)
#define QUIC_CLIENT_HISTOGRAM_TIMES_IMPL(name, sample, min, max, num_buckets, docstring)           \
  do {                                                                                             \
    (void)(sample);                                                                                \
  } while (0)
#define QUIC_CLIENT_HISTOGRAM_COUNTS_IMPL(name, sample, min, max, num_buckets, docstring)          \
  do {                                                                                             \
    (void)(sample);                                                                                \
  } while (0)

namespace quic {

inline void QuicClientSparseHistogramImpl(const std::string& /*name*/, int /*sample*/) {}

} // namespace quic
