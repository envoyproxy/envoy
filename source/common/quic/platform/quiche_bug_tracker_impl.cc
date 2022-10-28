#include "source/common/common/assert.h"

#include "quiche_platform_impl/quiche_bug_tracker_impl.h"
#include "quiche_platform_impl/quiche_logging_impl.h"

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quic {

std::atomic<bool> g_quiche_bug_exit_disabled;

ScopedDisableExitOnQuicheBug::ScopedDisableExitOnQuicheBug()
    : previous_value_(g_quiche_bug_exit_disabled) {

  g_quiche_bug_exit_disabled.store(true, std::memory_order_relaxed);
}

ScopedDisableExitOnQuicheBug::~ScopedDisableExitOnQuicheBug() {
  g_quiche_bug_exit_disabled.store(previous_value_, std::memory_order_relaxed);
}

QuicheBugEmitter::~QuicheBugEmitter() {
  // Release mode ENVOY_BUG applies rate limit.
  if (Envoy::Assert::shouldLogAndInvokeEnvoyBugForEnvoyBugMacroUseOnly(bug_name_)) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::envoy_bug), error,
                        "QUICHE_BUG failure: {}.{}{}", condition_str_,
                        stream_.eof() ? "" : " Details: ", stream_.str());
#if !defined(NDEBUG) && !defined(ENVOY_CONFIG_COVERAGE)
    if (!g_quiche_bug_exit_disabled) {
      abort();
    }
#else
    Envoy::Assert::invokeEnvoyBugFailureRecordActionForEnvoyBugMacroUseOnly(bug_name_.data());
#endif
  }
}

} // namespace quic
