#pragma once

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/event/dispatcher.h"

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/health_checkers/common/health_checker_base_impl.h"
#include "source/extensions/health_checkers/dynamic_modules/health_checker_config.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace DynamicModules {

/**
 * Interface implemented by a session to receive a health check result on the main thread. This
 * decouples the thread-safe control block and scheduler from the session's concrete type.
 */
class HealthCheckResultReceiver {
public:
  virtual ~HealthCheckResultReceiver() = default;

  /**
   * Apply a reported health status to the host. Always called on the main thread.
   */
  virtual void reportResult(envoy_dynamic_module_type_host_health health) PURE;
};

/**
 * Thread-safe link between a scheduler and its session. ``checker`` bounds the lifetime of the main
 * thread dispatcher: while it locks, the checker (and therefore its dispatcher) is alive.
 * ``receiver`` is the session; it is set and cleared only on the main thread and read only inside a
 * callback posted to the main thread, so no locking is required. The control block outlives the
 * session because the module's scheduler keeps a shared reference to it.
 */
struct DynamicModuleHealthCheckSessionControlBlock {
  DynamicModuleHealthCheckSessionControlBlock(
      std::weak_ptr<Upstream::HealthCheckerImplBase> checker, HealthCheckResultReceiver& receiver)
      : checker_(std::move(checker)), receiver_(&receiver) {}

  const std::weak_ptr<Upstream::HealthCheckerImplBase> checker_;
  HealthCheckResultReceiver* receiver_{nullptr};
};

using DynamicModuleHealthCheckSessionControlBlockSharedPtr =
    std::shared_ptr<DynamicModuleHealthCheckSessionControlBlock>;

/**
 * Thread-safe handle the module uses to report a health check result from any thread. Created via
 * envoy_dynamic_module_callback_health_checker_scheduler_new and destroyed via
 * envoy_dynamic_module_callback_health_checker_scheduler_delete.
 */
class DynamicModuleHealthCheckerScheduler {
public:
  explicit DynamicModuleHealthCheckerScheduler(
      DynamicModuleHealthCheckSessionControlBlockSharedPtr control_block)
      : control_block_(std::move(control_block)) {}

  // Posts the result to the main thread. May be called from any thread. The owning checker is
  // locked first so its dispatcher stays valid across ``post``; a report after the checker (or
  // session) has been torn down is a safe no-op.
  void report(envoy_dynamic_module_type_host_health health);

private:
  const DynamicModuleHealthCheckSessionControlBlockSharedPtr control_block_;
};

/**
 * Custom health checker that delegates the actual check to a dynamic module. Envoy drives the
 * standard per-host interval/timeout timers, thresholds and event logging via the base class; the
 * module performs the check (optionally on its own thread) and reports each host's health status
 * back through a scheduler.
 */
class DynamicModuleHealthChecker : public Upstream::HealthCheckerImplBase {
public:
  DynamicModuleHealthChecker(const Upstream::Cluster& cluster,
                             const envoy::config::core::v3::HealthCheck& config,
                             DynamicModuleHealthCheckerConfigSharedPtr module_config,
                             Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                             Random::RandomGenerator& random,
                             Upstream::HealthCheckEventLoggerPtr&& event_logger);

  // Accessor used by the scheduler, after locking the checker, to reach the main thread dispatcher.
  Event::Dispatcher& dispatcher() { return dispatcher_; }

  /**
   * Per-host health check session. Created on the main thread, one per host.
   */
  class DynamicModuleActiveHealthCheckSession : public ActiveHealthCheckSession,
                                                public HealthCheckResultReceiver {
  public:
    DynamicModuleActiveHealthCheckSession(DynamicModuleHealthChecker& parent,
                                          const Upstream::HostSharedPtr& host);
    ~DynamicModuleActiveHealthCheckSession() override;

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;
    void onDeferredDelete() final;

    // HealthCheckResultReceiver
    void reportResult(envoy_dynamic_module_type_host_health health) override;

    // Accessors used by the ABI callbacks.
    const Upstream::HostSharedPtr& host() const { return host_; }
    const DynamicModuleHealthCheckSessionControlBlockSharedPtr& controlBlock() const {
      return control_block_;
    }

  private:
    // Hold our own reference to the config (and therefore the loaded module) so it outlives this
    // session. The owning checker drops its reference before the base class tears down the
    // sessions, so relying on the checker's reference here would be a use-after-free.
    const DynamicModuleHealthCheckerConfigSharedPtr config_;
    envoy_dynamic_module_type_health_checker_session_module_ptr session_module_ptr_{nullptr};
    DynamicModuleHealthCheckSessionControlBlockSharedPtr control_block_;
    // True between onInterval() and a result/timeout. Guards against a late report being applied
    // after the check already resolved (e.g. a result arriving after a timeout).
    bool awaiting_result_{false};
  };

protected:
  envoy::data::core::v3::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v3::DYNAMIC_MODULE;
  }

private:
  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(Upstream::HostSharedPtr host) override {
    return std::make_unique<DynamicModuleActiveHealthCheckSession>(*this, host);
  }

  const DynamicModuleHealthCheckerConfigSharedPtr module_config_;
};

} // namespace DynamicModules
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
