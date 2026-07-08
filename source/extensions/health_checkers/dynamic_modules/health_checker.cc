#include "source/extensions/health_checkers/dynamic_modules/health_checker.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace DynamicModules {

DynamicModuleHealthChecker::DynamicModuleHealthChecker(
    const Upstream::Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
    DynamicModuleHealthCheckerConfigSharedPtr module_config, Event::Dispatcher& dispatcher,
    Runtime::Loader& runtime, Random::RandomGenerator& random,
    Upstream::HealthCheckEventLoggerPtr&& event_logger)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random, std::move(event_logger)),
      module_config_(std::move(module_config)) {}

void DynamicModuleHealthCheckerScheduler::report(envoy_dynamic_module_type_host_health health) {
  auto control_block = control_block_;
  // Lock the checker so its dispatcher stays valid across `post`. If it has been torn down, the
  // dispatcher (and the session) are gone too, so this is a safe no-op.
  auto checker = control_block->checker_.lock();
  if (checker == nullptr) {
    return;
  }
  // The control block's checker is always a DynamicModuleHealthChecker.
  auto& dispatcher = static_cast<DynamicModuleHealthChecker&>(*checker).dispatcher();
  // Move the locked checker into the posted callback so the last reference is always released on
  // the main thread (when the callback runs, or when the dispatcher is destroyed). Otherwise this
  // worker thread could drop the final reference and run ~HealthCheckerImplBase off the main
  // thread.
  dispatcher.post([control_block, checker = std::move(checker), health]() {
    if (control_block->receiver_ != nullptr) {
      control_block->receiver_->reportResult(health);
    }
  });
}

DynamicModuleHealthChecker::DynamicModuleActiveHealthCheckSession::
    DynamicModuleActiveHealthCheckSession(DynamicModuleHealthChecker& parent,
                                          const Upstream::HostSharedPtr& host)
    : ActiveHealthCheckSession(parent, host), config_(parent.module_config_),
      control_block_(std::make_shared<DynamicModuleHealthCheckSessionControlBlock>(
          parent.weak_from_this(), *this)) {
  session_module_ptr_ = config_->on_session_new_(config_->in_module_config_, this);
}

DynamicModuleHealthChecker::DynamicModuleActiveHealthCheckSession::
    ~DynamicModuleActiveHealthCheckSession() {
  // onDeferredDelete() performs teardown before destruction (the base guarantees it runs and
  // asserts the same in its own destructor).
  ASSERT(session_module_ptr_ == nullptr);
}

void DynamicModuleHealthChecker::DynamicModuleActiveHealthCheckSession::onInterval() {
  if (session_module_ptr_ == nullptr) {
    return;
  }
  awaiting_result_ = true;
  config_->on_session_on_interval_(session_module_ptr_, this);
}

void DynamicModuleHealthChecker::DynamicModuleActiveHealthCheckSession::onTimeout() {
  awaiting_result_ = false;
  if (session_module_ptr_ != nullptr && config_->on_session_on_timeout_ != nullptr) {
    config_->on_session_on_timeout_(session_module_ptr_);
  }
}

void DynamicModuleHealthChecker::DynamicModuleActiveHealthCheckSession::onDeferredDelete() {
  awaiting_result_ = false;
  control_block_->receiver_ = nullptr;
  if (session_module_ptr_ != nullptr) {
    config_->on_session_destroy_(session_module_ptr_);
    session_module_ptr_ = nullptr;
  }
}

void DynamicModuleHealthChecker::DynamicModuleActiveHealthCheckSession::reportResult(
    envoy_dynamic_module_type_host_health health) {
  // Ignore a result that arrives after the check already resolved (e.g. after a timeout) or after
  // the session was torn down.
  if (!awaiting_result_) {
    return;
  }
  awaiting_result_ = false;
  switch (health) {
  case envoy_dynamic_module_type_host_health_Healthy:
    handleSuccess(false);
    break;
  case envoy_dynamic_module_type_host_health_Degraded:
    handleSuccess(true);
    break;
  case envoy_dynamic_module_type_host_health_Unhealthy:
    handleFailure(envoy::data::core::v3::ACTIVE);
    break;
  }
}

} // namespace DynamicModules
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
