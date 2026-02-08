// NOLINT(namespace-envoy)

// This file provides host-side implementations for ABI callbacks specific to bootstrap extensions.

#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"
#include "source/extensions/bootstrap/dynamic_modules/extension.h"
#include "source/extensions/bootstrap/dynamic_modules/extension_config.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

using Envoy::Extensions::Bootstrap::DynamicModules::DynamicModuleBootstrapExtension;
using Envoy::Extensions::Bootstrap::DynamicModules::DynamicModuleBootstrapExtensionConfig;
using Envoy::Extensions::Bootstrap::DynamicModules::DynamicModuleBootstrapExtensionConfigScheduler;

extern "C" {

envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr
envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(extension_config_envoy_ptr);
  return new DynamicModuleBootstrapExtensionConfigScheduler(config->weak_from_this(),
                                                            config->main_thread_dispatcher_);
}

void envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(
    envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr
        scheduler_module_ptr) {
  delete static_cast<DynamicModuleBootstrapExtensionConfigScheduler*>(scheduler_module_ptr);
}

void envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(
    envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id) {
  auto* scheduler =
      static_cast<DynamicModuleBootstrapExtensionConfigScheduler*>(scheduler_module_ptr);
  scheduler->commit(event_id);
}

// -------------------- HTTP Callout Callbacks --------------------

envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_bootstrap_extension_http_callout(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    uint64_t* callout_id_out, envoy_dynamic_module_type_module_buffer cluster_name,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_module_buffer body, uint64_t timeout_milliseconds) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(extension_config_envoy_ptr);

  // Build the HTTP request message.
  Envoy::Http::RequestHeaderMapPtr header_map = Envoy::Http::RequestHeaderMapImpl::create();
  for (size_t i = 0; i < headers_size; ++i) {
    header_map->addCopy(
        Envoy::Http::LowerCaseString(std::string(headers[i].key_ptr, headers[i].key_length)),
        std::string(headers[i].value_ptr, headers[i].value_length));
  }

  // Check required headers.
  if (header_map->Path() == nullptr || header_map->Method() == nullptr ||
      header_map->Host() == nullptr) {
    return envoy_dynamic_module_type_http_callout_init_result_MissingRequiredHeaders;
  }

  Envoy::Http::RequestMessagePtr message =
      std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(header_map));

  if (body.length > 0 && body.ptr != nullptr) {
    message->body().add(absl::string_view(body.ptr, body.length));
  }

  return config->sendHttpCallout(callout_id_out,
                                 absl::string_view(cluster_name.ptr, cluster_name.length),
                                 std::move(message), timeout_milliseconds);
}

// -------------------- Stats Access Callbacks --------------------

bool envoy_dynamic_module_callback_bootstrap_extension_get_counter_value(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, uint64_t* value_ptr) {
  auto* extension = static_cast<DynamicModuleBootstrapExtension*>(extension_envoy_ptr);
  Envoy::Stats::Store& stats_store = extension->statsStore();
  const absl::string_view name_view(name.ptr, name.length);

  // Use iterate() instead of forEachCounter() to enable early exit once the stat is found.
  bool found = false;
  Envoy::Stats::IterateFn<Envoy::Stats::Counter> counter_callback =
      [&name_view, &found, value_ptr](const Envoy::Stats::CounterSharedPtr& counter) -> bool {
    if (counter->name() == name_view) {
      *value_ptr = counter->value();
      found = true;
      return false; // Stop iteration.
    }
    return true; // Continue iteration.
  };
  stats_store.iterate(counter_callback);
  return found;
}

bool envoy_dynamic_module_callback_bootstrap_extension_get_gauge_value(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, uint64_t* value_ptr) {
  auto* extension = static_cast<DynamicModuleBootstrapExtension*>(extension_envoy_ptr);
  Envoy::Stats::Store& stats_store = extension->statsStore();
  const absl::string_view name_view(name.ptr, name.length);

  // Use iterate() instead of forEachGauge() to enable early exit once the stat is found.
  bool found = false;
  Envoy::Stats::IterateFn<Envoy::Stats::Gauge> gauge_callback =
      [&name_view, &found, value_ptr](const Envoy::Stats::GaugeSharedPtr& gauge) -> bool {
    if (gauge->name() == name_view) {
      *value_ptr = gauge->value();
      found = true;
      return false; // Stop iteration.
    }
    return true; // Continue iteration.
  };
  stats_store.iterate(gauge_callback);
  return found;
}

bool envoy_dynamic_module_callback_bootstrap_extension_get_histogram_summary(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, uint64_t* sample_count_ptr,
    double* sample_sum_ptr) {
  auto* extension = static_cast<DynamicModuleBootstrapExtension*>(extension_envoy_ptr);
  Envoy::Stats::Store& stats_store = extension->statsStore();
  const absl::string_view name_view(name.ptr, name.length);

  bool found = false;
  stats_store.forEachHistogram(
      [](size_t) {},
      [&name_view, &found, sample_count_ptr, sample_sum_ptr](Envoy::Stats::ParentHistogram& hist) {
        if (!found && hist.name() == name_view) {
          const auto& stats = hist.cumulativeStatistics();
          *sample_count_ptr = stats.sampleCount();
          *sample_sum_ptr = stats.sampleSum();
          found = true;
        }
      });
  return found;
}

void envoy_dynamic_module_callback_bootstrap_extension_iterate_counters(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_counter_iterator_fn iterator_fn, void* user_data) {
  auto* extension = static_cast<DynamicModuleBootstrapExtension*>(extension_envoy_ptr);
  Envoy::Stats::Store& stats_store = extension->statsStore();

  stats_store.forEachCounter([](size_t) {},
                             [iterator_fn, user_data](Envoy::Stats::Counter& counter) {
                               std::string name = counter.name();
                               envoy_dynamic_module_type_envoy_buffer name_buffer{name.data(),
                                                                                  name.size()};
                               auto action = iterator_fn(name_buffer, counter.value(), user_data);
                               // Note: forEachCounter doesn't support early exit, so we ignore Stop
                               // action. The module should handle this by setting a flag in
                               // user_data.
                               (void)action;
                             });
}

void envoy_dynamic_module_callback_bootstrap_extension_iterate_gauges(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_gauge_iterator_fn iterator_fn, void* user_data) {
  auto* extension = static_cast<DynamicModuleBootstrapExtension*>(extension_envoy_ptr);
  Envoy::Stats::Store& stats_store = extension->statsStore();

  stats_store.forEachGauge([](size_t) {},
                           [iterator_fn, user_data](Envoy::Stats::Gauge& gauge) {
                             std::string name = gauge.name();
                             envoy_dynamic_module_type_envoy_buffer name_buffer{name.data(),
                                                                                name.size()};
                             auto action = iterator_fn(name_buffer, gauge.value(), user_data);
                             // Note: forEachGauge doesn't support early exit, so we ignore Stop
                             // action. The module should handle this by setting a flag in
                             // user_data.
                             (void)action;
                           });
}

// -------------------- Stats Definition and Update Callbacks --------------------

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* counter_id_ptr) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);
  Envoy::Stats::StatName stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Envoy::Stats::Counter& c =
      Envoy::Stats::Utility::counterFromStatNames(*config->stats_scope_, {stat_name});
  *counter_id_ptr = config->addCounter({c});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);
  auto counter = config->getCounterById(id);
  if (!counter.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  counter->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* gauge_id_ptr) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);
  Envoy::Stats::StatName stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Envoy::Stats::Gauge& g = Envoy::Stats::Utility::gaugeFromStatNames(
      *config->stats_scope_, {stat_name}, Envoy::Stats::Gauge::ImportMode::Accumulate);
  *gauge_id_ptr = config->addGauge({g});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);
  auto gauge = config->getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->set(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);
  auto gauge = config->getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);
  auto gauge = config->getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->sub(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* histogram_id_ptr) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);
  Envoy::Stats::StatName stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Envoy::Stats::Histogram& h = Envoy::Stats::Utility::histogramFromStatNames(
      *config->stats_scope_, {stat_name}, Envoy::Stats::Histogram::Unit::Unspecified);
  *histogram_id_ptr = config->addHistogram({h});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);
  auto histogram = config->getHistogramById(id);
  if (!histogram.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  histogram->recordValue(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

} // extern "C"
