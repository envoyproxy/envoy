#include "source/extensions/stat_sinks/dynamic_modules/sink_config.h"

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

DynamicModuleStatsSinkConfig::DynamicModuleStatsSinkConfig(
    absl::string_view sink_name, absl::string_view sink_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Server::Configuration::ServerFactoryContext& server)
    : main_thread_dispatcher_(server.mainThreadDispatcher()), sink_name_(sink_name),
      sink_config_(sink_config), dynamic_module_(std::move(dynamic_module)),
      stats_scope_(server.scope().createScope("")), stat_name_pool_(stats_scope_->symbolTable()) {}

DynamicModuleStatsSinkConfig::~DynamicModuleStatsSinkConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

envoy_dynamic_module_type_metrics_result
DynamicModuleStatsSinkConfig::defineGauge(absl::string_view name, size_t* gauge_id_out) {
  // Defining a gauge mutates shared storage, so reject calls from other threads.
  if (!Thread::MainThread::isMainOrTestThread()) {
    IS_ENVOY_BUG("envoy_dynamic_module_callback_stat_sink_config_define_gauge must be called on "
                 "the main thread");
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  // Acquire-load pairs with the release-store in newDynamicModuleStatsSinkConfig(). See the header
  // for the memory-order contract.
  if (stat_creation_frozen_.load(std::memory_order_acquire)) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  Stats::StatName stat_name = stat_name_pool_.add(name);
  Stats::Gauge& gauge = Stats::Utility::gaugeFromStatNames(*stats_scope_, {stat_name},
                                                           Stats::Gauge::ImportMode::Accumulate);
  gauges_.emplace_back(gauge);
  *gauge_id_out = gauges_.size(); // 1-based id so 0 stays reserved as invalid.
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result DynamicModuleStatsSinkConfig::setGauge(size_t gauge_id,
                                                                                uint64_t value) {
  // Setting a gauge writes shared state, so reject calls from other threads.
  if (!Thread::MainThread::isMainOrTestThread()) {
    IS_ENVOY_BUG("envoy_dynamic_module_callback_stat_sink_config_set_gauge must be called on the "
                 "main thread");
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (gauge_id == 0 || gauge_id > gauges_.size()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauges_[gauge_id - 1].set(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

void DynamicModuleStatsSinkConfig::onScheduled(uint64_t event_id) {
  if (on_config_scheduled_ != nullptr) {
    on_config_scheduled_(this, in_module_config_, event_id);
  }
}

absl::StatusOr<DynamicModuleStatsSinkConfigSharedPtr>
newDynamicModuleStatsSinkConfig(absl::string_view sink_name, absl::string_view sink_config,
                                Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                Server::Configuration::ServerFactoryContext& server) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  auto on_config_new = dynamic_module->getFunctionPointer<OnStatSinkConfigNewType>(
      "envoy_dynamic_module_on_stat_sink_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnStatSinkConfigDestroyType>(
      "envoy_dynamic_module_on_stat_sink_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_flush = dynamic_module->getFunctionPointer<OnStatSinkFlushType>(
      "envoy_dynamic_module_on_stat_sink_flush");
  RETURN_IF_NOT_OK_REF(on_flush.status());

  auto on_histogram_complete =
      dynamic_module->getFunctionPointer<OnStatSinkOnHistogramCompleteType>(
          "envoy_dynamic_module_on_stat_sink_on_histogram_complete");
  RETURN_IF_NOT_OK_REF(on_histogram_complete.status());

  // Optional. Modules that do not schedule events do not need to implement it.
  auto on_config_scheduled = dynamic_module->getFunctionPointer<OnStatSinkConfigScheduledType>(
      "envoy_dynamic_module_on_stat_sink_config_scheduled");

  auto config = std::make_shared<DynamicModuleStatsSinkConfig>(sink_name, sink_config,
                                                               std::move(dynamic_module), server);

  config->on_config_destroy_ = on_config_destroy.value();
  config->on_flush_ = on_flush.value();
  config->on_histogram_complete_ = on_histogram_complete.value();
  if (on_config_scheduled.ok()) {
    config->on_config_scheduled_ = on_config_scheduled.value();
  }

  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = config->sink_name_.data(),
                                                     .length = config->sink_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buf = {.ptr = config->sink_config_.data(),
                                                       .length = config->sink_config_.size()};
  config->in_module_config_ =
      (*on_config_new.value())(static_cast<void*>(config.get()), name_buf, config_buf);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module stats sink config");
  }

  // Release-store pairs with the acquire-load in defineGauge(). After this point the module can
  // only set previously defined gauges, never define new ones, so the gauge storage is read-only.
  config->stat_creation_frozen_.store(true, std::memory_order_release);
  return config;
}

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
