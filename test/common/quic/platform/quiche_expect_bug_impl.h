#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "test/test_common/utility.h"

#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_mock_log.h"

#define EXPECT_QUICHE_BUG_IMPL(statement, regex) EXPECT_ENVOY_BUG(statement, regex)

#define EXPECT_QUICHE_PEER_BUG_IMPL(statement, regex)                                              \
  EXPECT_QUICHE_LOG_IMPL(statement, ERROR, testing::ContainsRegex(regex))
