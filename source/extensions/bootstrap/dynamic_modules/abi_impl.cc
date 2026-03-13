// NOLINT(namespace-envoy)

// This file provides host-side implementations for ABI callbacks specific to bootstrap extensions.

#include "envoy/server/admin.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"
#include "source/extensions/bootstrap/dynamic_modules/extension.h"
#include "source/extensions/bootstrap/dynamic_modules/extension_config.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

using Envoy::Extensions::Bootstrap::DynamicModules::DynamicModuleBootstrapExtension;
using Envoy::Extensions::Bootstrap::DynamicModules::DynamicModuleBootstrapExtensionConfig;
using Envoy::Extensions::Bootstrap::DynamicModules::DynamicModuleBootstrapExtensionConfigScheduler;
using Envoy::Extensions::Bootstrap::DynamicModules::DynamicModuleBootstrapExtensionTimer;

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

// -------------------- Init Manager Callbacks --------------------

void envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(extension_config_envoy_ptr);
  config->signalInitComplete();
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

} // extern "C"

namespace {

// Helper to build a StatNameTagVector from label names and label values.
Envoy::Stats::StatNameTagVector buildTagsForBootstrapMetric(
    DynamicModuleBootstrapExtensionConfig& config, const Envoy::Stats::StatNameVec& label_names,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length) {
  ASSERT(label_values_length == label_names.size());
  Envoy::Stats::StatNameTagVector tags;
  tags.reserve(label_values_length);
  for (size_t i = 0; i < label_values_length; i++) {
    absl::string_view label_value_view(label_values[i].ptr, label_values[i].length);
    auto label_value = config.stat_name_pool_.add(label_value_view);
    tags.push_back(Envoy::Stats::StatNameTag(label_names[i], label_value));
  }
  return tags;
}

} // namespace

extern "C" {

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);
  absl::string_view name_view(name.ptr, name.length);
  Envoy::Stats::StatName main_stat_name = config->stat_name_pool_.add(name_view);

  // Handle the special case where the labels size is zero.
  if (label_names_length == 0) {
    Envoy::Stats::Counter& c =
        Envoy::Stats::Utility::counterFromStatNames(*config->stats_scope_, {main_stat_name});
    *counter_id_ptr = config->addCounter({c});
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(config->stat_name_pool_.add(label_name_view));
  }
  *counter_id_ptr = config->addCounterVec({main_stat_name, label_names_vec});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);

  // Handle the special case where the labels size is zero.
  if (label_values_length == 0) {
    auto counter = config->getCounterById(id);
    if (!counter.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    counter->add(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  auto counter = config->getCounterVecById(id);
  if (!counter.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != counter->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags = buildTagsForBootstrapMetric(*config, counter->getLabelNames(), label_values,
                                          label_values_length);
  counter->add(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);
  absl::string_view name_view(name.ptr, name.length);
  Envoy::Stats::StatName main_stat_name = config->stat_name_pool_.add(name_view);
  Envoy::Stats::Gauge::ImportMode import_mode = Envoy::Stats::Gauge::ImportMode::Accumulate;

  // Handle the special case where the labels size is zero.
  if (label_names_length == 0) {
    Envoy::Stats::Gauge& g = Envoy::Stats::Utility::gaugeFromStatNames(
        *config->stats_scope_, {main_stat_name}, import_mode);
    *gauge_id_ptr = config->addGauge({g});
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(config->stat_name_pool_.add(label_name_view));
  }
  *gauge_id_ptr = config->addGaugeVec({main_stat_name, label_names_vec, import_mode});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);
  // Handle the special case where the labels size is zero.
  if (label_values_length == 0) {
    auto gauge = config->getGaugeById(id);
    if (!gauge.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    gauge->set(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }
  auto gauge = config->getGaugeVecById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != gauge->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags = buildTagsForBootstrapMetric(*config, gauge->getLabelNames(), label_values,
                                          label_values_length);
  gauge->set(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);
  // Handle the special case where the labels size is zero.
  if (label_values_length == 0) {
    auto gauge = config->getGaugeById(id);
    if (!gauge.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    gauge->add(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }
  auto gauge = config->getGaugeVecById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != gauge->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags = buildTagsForBootstrapMetric(*config, gauge->getLabelNames(), label_values,
                                          label_values_length);
  gauge->add(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);
  // Handle the special case where the labels size is zero.
  if (label_values_length == 0) {
    auto gauge = config->getGaugeById(id);
    if (!gauge.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    gauge->sub(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }
  auto gauge = config->getGaugeVecById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != gauge->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags = buildTagsForBootstrapMetric(*config, gauge->getLabelNames(), label_values,
                                          label_values_length);
  gauge->sub(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);
  absl::string_view name_view(name.ptr, name.length);
  Envoy::Stats::StatName main_stat_name = config->stat_name_pool_.add(name_view);
  Envoy::Stats::Histogram::Unit unit = Envoy::Stats::Histogram::Unit::Unspecified;

  // Handle the special case where the labels size is zero.
  if (label_names_length == 0) {
    Envoy::Stats::Histogram& h = Envoy::Stats::Utility::histogramFromStatNames(
        *config->stats_scope_, {main_stat_name}, unit);
    *histogram_id_ptr = config->addHistogram({h});
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(config->stat_name_pool_.add(label_name_view));
  }
  *histogram_id_ptr = config->addHistogramVec({main_stat_name, label_names_vec, unit});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(config_envoy_ptr);
  // Handle the special case where the labels size is zero.
  if (label_values_length == 0) {
    auto histogram = config->getHistogramById(id);
    if (!histogram.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    histogram->recordValue(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }
  auto histogram = config->getHistogramVecById(id);
  if (!histogram.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != histogram->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags = buildTagsForBootstrapMetric(*config, histogram->getLabelNames(), label_values,
                                          label_values_length);
  histogram->recordValue(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

// -------------------- Timer Callbacks --------------------

envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr
envoy_dynamic_module_callback_bootstrap_extension_timer_new(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(extension_config_envoy_ptr);

  // Allocate the timer wrapper first so we can capture a stable heap pointer in the callback.
  auto* timer_wrapper = new DynamicModuleBootstrapExtensionTimer(config->weak_from_this());

  // Create the timer on the main thread dispatcher. The callback captures a weak_ptr to the config
  // to safely handle the case where the config is destroyed before the timer fires. The
  // timer_wrapper raw pointer is captured by value (copied) and is stable since it is
  // heap-allocated and its lifetime is managed by the module via timer_new/timer_delete.
  auto envoy_timer = config->main_thread_dispatcher_.createTimer(
      [weak_config = config->weak_from_this(), timer_wrapper]() {
        if (auto config_shared = weak_config.lock()) {
          if (config_shared->in_module_config_ != nullptr &&
              config_shared->on_bootstrap_extension_timer_fired_ != nullptr) {
            config_shared->on_bootstrap_extension_timer_fired_(config_shared->thisAsVoidPtr(),
                                                               config_shared->in_module_config_,
                                                               static_cast<void*>(timer_wrapper));
          }
        }
      });

  timer_wrapper->setTimer(std::move(envoy_timer));
  return static_cast<void*>(timer_wrapper);
}

void envoy_dynamic_module_callback_bootstrap_extension_timer_enable(
    envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr timer_ptr,
    uint64_t delay_milliseconds) {
  auto* timer = static_cast<DynamicModuleBootstrapExtensionTimer*>(timer_ptr);
  timer->timer().enableTimer(std::chrono::milliseconds(delay_milliseconds));
}

void envoy_dynamic_module_callback_bootstrap_extension_timer_disable(
    envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr timer_ptr) {
  auto* timer = static_cast<DynamicModuleBootstrapExtensionTimer*>(timer_ptr);
  timer->timer().disableTimer();
}

bool envoy_dynamic_module_callback_bootstrap_extension_timer_enabled(
    envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr timer_ptr) {
  auto* timer = static_cast<DynamicModuleBootstrapExtensionTimer*>(timer_ptr);
  return timer->timer().enabled();
}

void envoy_dynamic_module_callback_bootstrap_extension_timer_delete(
    envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr timer_ptr) {
  delete static_cast<DynamicModuleBootstrapExtensionTimer*>(timer_ptr);
}

// -------------------- Admin Handler Callbacks --------------------

void envoy_dynamic_module_callback_bootstrap_extension_admin_set_response(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer response_body) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(extension_config_envoy_ptr);
  if (response_body.ptr != nullptr && response_body.length > 0) {
    config->admin_response_body_.assign(response_body.ptr, response_body.length);
  }
}

bool envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer path_prefix,
    envoy_dynamic_module_type_module_buffer help_text, bool removable, bool mutates_server_state) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(extension_config_envoy_ptr);
  Envoy::OptRef<Envoy::Server::Admin> admin = config->context_.admin();
  if (!admin.has_value()) {
    return false;
  }

  const std::string prefix_str(path_prefix.ptr, path_prefix.length);
  const std::string help_str(help_text.ptr, help_text.length);

  // Capture a shared_ptr to the config to ensure it stays alive during admin handler callbacks.
  auto config_shared = config->shared_from_this();

  return admin->addHandler(
      prefix_str, help_str,
      [config_shared](Envoy::Http::ResponseHeaderMap& response_headers,
                      Envoy::Buffer::Instance& response,
                      Envoy::Server::AdminStream& admin_stream) -> Envoy::Http::Code {
        const auto& request_headers = admin_stream.getRequestHeaders();
        const auto method_entry = request_headers.getMethodValue();
        const auto path_entry = request_headers.getPathValue();
        const std::string method_str(method_entry.data(), method_entry.size());
        const std::string path_str(path_entry.data(), path_entry.size());

        std::string body_str;
        const Envoy::Buffer::Instance* request_body = admin_stream.getRequestBody();
        if (request_body != nullptr && request_body->length() > 0) {
          body_str = request_body->toString();
        }

        envoy_dynamic_module_type_envoy_buffer method_buf{method_str.data(), method_str.size()};
        envoy_dynamic_module_type_envoy_buffer path_buf{path_str.data(), path_str.size()};
        envoy_dynamic_module_type_envoy_buffer body_buf{body_str.data(), body_str.size()};

        // Clear any previous response body before calling the event hook.
        config_shared->admin_response_body_.clear();

        uint32_t status_code = config_shared->on_bootstrap_extension_admin_request_(
            config_shared->thisAsVoidPtr(), config_shared->in_module_config_, method_buf, path_buf,
            body_buf);

        if (!config_shared->admin_response_body_.empty()) {
          response.add(config_shared->admin_response_body_);
        }

        // Set content-type to text/plain by default.
        response_headers.setReferenceContentType(
            Envoy::Http::Headers::get().ContentTypeValues.Text);

        return static_cast<Envoy::Http::Code>(status_code);
      },
      removable, mutates_server_state);
}

bool envoy_dynamic_module_callback_bootstrap_extension_remove_admin_handler(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer path_prefix) {
  auto* config = static_cast<DynamicModuleBootstrapExtensionConfig*>(extension_config_envoy_ptr);
  Envoy::OptRef<Envoy::Server::Admin> admin = config->context_.admin();
  if (!admin.has_value()) {
    return false;
  }

  const std::string prefix_str(path_prefix.ptr, path_prefix.length);
  return admin->removeHandler(prefix_str);
}

} // extern "C"
