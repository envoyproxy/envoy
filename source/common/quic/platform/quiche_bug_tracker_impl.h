#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <sstream>

#include "absl/strings/string_view.h"

namespace quic {

class QuicheBugEmitter {
public:
  explicit QuicheBugEmitter(absl::string_view condition_str, absl::string_view bug_name)
      : condition_str_(condition_str), bug_name_(bug_name) {}

  ~QuicheBugEmitter();

  std::ostringstream& stream() { return stream_; }

private:
  std::ostringstream stream_;
  const std::string condition_str_;
  const std::string bug_name_;
};

// Test and fuzz only, not for production, not thread-safe.
class ScopedDisableExitOnQuicheBug {
public:
  ScopedDisableExitOnQuicheBug();
  ~ScopedDisableExitOnQuicheBug();

private:
  const bool previous_value_;
};

} // namespace quic

#define QUICHE_BUG_IF_IMPL(bug_id, condition)                                                      \
  switch (0)                                                                                       \
  default:                                                                                         \
    if (!(condition)) {                                                                            \
    } else                                                                                         \
      quic::QuicheBugEmitter(#condition, #bug_id).stream()

#define QUICHE_BUG_IMPL(bug_id) QUICHE_BUG_IF_IMPL(bug_id, true)

#define QUICHE_PEER_BUG_IMPL(bug_id) QUICHE_LOG_IMPL(ERROR)
#define QUICHE_PEER_BUG_IF_IMPL(bug_id, condition) QUICHE_LOG_IF_IMPL(ERROR, condition)
