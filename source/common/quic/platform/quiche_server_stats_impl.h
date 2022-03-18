#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#define QUICHE_SERVER_HISTOGRAM_ENUM_IMPL(name, sample, enum_size, docstring)                      \
  do {                                                                                             \
  } while (0)

#define QUICHE_SERVER_HISTOGRAM_BOOL_IMPL(name, sample, docstring)                                 \
  do {                                                                                             \
  } while (0)

#define QUICHE_SERVER_HISTOGRAM_TIMES_IMPL(name, sample, min, max, bucket_count, docstring)        \
  do {                                                                                             \
  } while (0)

#define QUICHE_SERVER_HISTOGRAM_COUNTS_IMPL(name, sample, min, max, bucket_count, docstring)       \
  do {                                                                                             \
  } while (0)
