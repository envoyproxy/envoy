#pragma once

// NOLINT(namespace-envoy)
// A wrapper of QUICHE includes which suppresses some compilation warnings thrown from QUICHE.

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#ifdef QUICHE_INCLUDE_1
#include QUICHE_INCLUDE_1

#endif
#ifdef QUICHE_INCLUDE_2
#include QUICHE_INCLUDE_2
#endif
#ifdef QUICHE_INCLUDE_3
#include QUICHE_INCLUDE_3
#endif
#ifdef QUICHE_INCLUDE_4
#include QUICHE_INCLUDE_4
#endif
#ifdef QUICHE_INCLUDE_5
#include QUICHE_INCLUDE_5
#endif
#ifdef QUICHE_INCLUDE_6
#include QUICHE_INCLUDE_6
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
