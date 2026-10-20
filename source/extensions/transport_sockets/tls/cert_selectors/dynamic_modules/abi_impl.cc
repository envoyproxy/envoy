#include "envoy/network/transport_socket.h"

#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"
#include "source/common/tls/context_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/abi_context_accessors.h"
#include "source/extensions/dynamic_modules/metric_registry.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/tls_certificate_selector.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace DynamicModules {

using Envoy::Extensions::DynamicModules::ContextAccessor;
using Envoy::Extensions::DynamicModules::MetricRegistry;

namespace {

DynamicModuleTlsCertificateSelectorConfig*
toConfig(envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr) {
  return static_cast<DynamicModuleTlsCertificateSelectorConfig*>(config_envoy_ptr);
}

const TlsCertificateSelectionContext&
toContext(envoy_dynamic_module_type_tls_certificate_selector_context_envoy_ptr context_envoy_ptr) {
  return *static_cast<const TlsCertificateSelectionContext*>(context_envoy_ptr);
}

absl::string_view toStringView(envoy_dynamic_module_type_module_buffer buffer) {
  return {buffer.ptr, buffer.length};
}

// Returns the stream info of the connection running the handshake, or nullptr when the handshake
// is not attached to a connection.
const StreamInfo::StreamInfo* connectionStreamInfo(const TlsCertificateSelectionContext& context) {
  auto* callbacks = static_cast<Network::TransportSocketCallbacks*>(
      SSL_get_ex_data(&context.ssl, ContextImpl::sslSocketIndex()));
  return callbacks != nullptr ? &callbacks->connection().streamInfo() : nullptr;
}

// Returns the registry to define a metric in, or nothing once the configuration is created.
OptRef<MetricRegistry> definableMetrics(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr) {
  DynamicModuleTlsCertificateSelectorConfig* config = toConfig(config_envoy_ptr);
  if (config->stat_creation_frozen_.load(std::memory_order_acquire)) {
    return {};
  }
  return config->metrics();
}

Stats::StatNameVec labelNames(MetricRegistry& metrics,
                              envoy_dynamic_module_type_module_buffer* label_names,
                              size_t label_names_length) {
  Stats::StatNameVec names;
  names.reserve(label_names_length);
  for (size_t i = 0; i < label_names_length; ++i) {
    names.push_back(metrics.statNamePool().add(toStringView(label_names[i])));
  }
  return names;
}

// Records a value in the metric of the id. A metric defined without labels takes no label values,
// and one defined with labels takes a value for each of its label names.
template <class Metric, class MetricVec>
envoy_dynamic_module_type_metrics_result
recordMetric(envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr,
             size_t id, envoy_dynamic_module_type_module_buffer* label_values,
             size_t label_values_length, uint64_t value,
             OptRef<const Metric> (MetricRegistry::*get_metric)(size_t) const,
             OptRef<const MetricVec> (MetricRegistry::*get_metric_vec)(size_t) const,
             void (Metric::*record)(uint64_t) const,
             void (MetricVec::*record_vec)(Stats::Scope&, Stats::StatNameTagVectorOptConstRef,
                                           uint64_t) const) {
  MetricRegistry& metrics = toConfig(config_envoy_ptr)->metrics();
  if (const OptRef<const Metric> metric = (metrics.*get_metric)(id); metric.has_value()) {
    if (label_values_length != 0) {
      return envoy_dynamic_module_type_metrics_result_InvalidLabels;
    }
    ((*metric).*record)(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }
  const OptRef<const MetricVec> metric_vec = (metrics.*get_metric_vec)(id);
  if (!metric_vec.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != metric_vec->labelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  Stats::StatNameDynamicPool dynamic_pool(metrics.scope().symbolTable());
  Stats::StatNameTagVector tags;
  tags.reserve(label_values_length);
  for (size_t i = 0; i < label_values_length; ++i) {
    tags.emplace_back(metric_vec->labelNames()[i], dynamic_pool.add(toStringView(label_values[i])));
  }
  ((*metric_vec).*record_vec)(metrics.scope(), tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

} // namespace

extern "C" {

bool envoy_dynamic_module_callback_tls_certificate_selector_get_requested_server_name(
    envoy_dynamic_module_type_tls_certificate_selector_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_out) {
  const absl::string_view server_name = absl::NullSafeStringView(
      SSL_get_servername(&toContext(context_envoy_ptr).ssl, TLSEXT_NAMETYPE_host_name));
  *result_out = {server_name.data(), server_name.size()};
  return !server_name.empty();
}

bool envoy_dynamic_module_callback_tls_certificate_selector_get_client_hello_extension(
    envoy_dynamic_module_type_tls_certificate_selector_context_envoy_ptr context_envoy_ptr,
    uint16_t extension_type, envoy_dynamic_module_type_envoy_buffer* result_out) {
  const SSL_CLIENT_HELLO* client_hello = toContext(context_envoy_ptr).client_hello;
  const uint8_t* data = nullptr;
  size_t length = 0;
  if (client_hello == nullptr ||
      !SSL_early_callback_ctx_extension_get(client_hello, extension_type, &data, &length)) {
    return false;
  }
  *result_out = {reinterpret_cast<const char*>(data), length};
  return true;
}

bool envoy_dynamic_module_callback_tls_certificate_selector_get_filter_state(
    envoy_dynamic_module_type_tls_certificate_selector_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* value_out) {
  const StreamInfo::StreamInfo* stream_info = connectionStreamInfo(toContext(context_envoy_ptr));
  return stream_info != nullptr &&
         ContextAccessor::getFilterStateBytes(*stream_info, key, value_out);
}

bool envoy_dynamic_module_callback_tls_certificate_selector_config_set_certificate(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer certificate_chain,
    envoy_dynamic_module_type_module_buffer private_key,
    envoy_dynamic_module_type_module_buffer ocsp_staple) {
  return toConfig(config_envoy_ptr)
      ->setCertificate(toStringView(name), toStringView(certificate_chain),
                       toStringView(private_key), toStringView(ocsp_staple));
}

void envoy_dynamic_module_callback_tls_certificate_selector_config_remove_certificate(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name) {
  toConfig(config_envoy_ptr)->removeCertificate(toStringView(name));
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tls_certificate_selector_config_define_counter(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr) {
  OptRef<MetricRegistry> metrics = definableMetrics(config_envoy_ptr);
  if (!metrics.has_value()) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  const Stats::StatName stat_name = metrics->statNamePool().add(toStringView(name));
  if (label_names_length == 0) {
    *counter_id_ptr = metrics->addCounter(MetricRegistry::CounterHandle(
        Stats::Utility::counterFromStatNames(metrics->scope(), {stat_name})));
  } else {
    *counter_id_ptr = metrics->addCounterVec(MetricRegistry::CounterVecHandle(
        stat_name, labelNames(*metrics, label_names, label_names_length)));
  }
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tls_certificate_selector_config_increment_counter(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  return recordMetric(config_envoy_ptr, id, label_values, label_values_length, value,
                      &MetricRegistry::getCounterById, &MetricRegistry::getCounterVecById,
                      &MetricRegistry::CounterHandle::add, &MetricRegistry::CounterVecHandle::add);
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tls_certificate_selector_config_define_gauge(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr) {
  OptRef<MetricRegistry> metrics = definableMetrics(config_envoy_ptr);
  if (!metrics.has_value()) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  const Stats::StatName stat_name = metrics->statNamePool().add(toStringView(name));
  const Stats::Gauge::ImportMode import_mode = Stats::Gauge::ImportMode::Accumulate;
  if (label_names_length == 0) {
    *gauge_id_ptr = metrics->addGauge(MetricRegistry::GaugeHandle(
        Stats::Utility::gaugeFromStatNames(metrics->scope(), {stat_name}, import_mode)));
  } else {
    *gauge_id_ptr = metrics->addGaugeVec(MetricRegistry::GaugeVecHandle(
        stat_name, labelNames(*metrics, label_names, label_names_length), import_mode));
  }
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tls_certificate_selector_config_set_gauge(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  return recordMetric(config_envoy_ptr, id, label_values, label_values_length, value,
                      &MetricRegistry::getGaugeById, &MetricRegistry::getGaugeVecById,
                      &MetricRegistry::GaugeHandle::set, &MetricRegistry::GaugeVecHandle::set);
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tls_certificate_selector_config_increment_gauge(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  return recordMetric(config_envoy_ptr, id, label_values, label_values_length, value,
                      &MetricRegistry::getGaugeById, &MetricRegistry::getGaugeVecById,
                      &MetricRegistry::GaugeHandle::increase,
                      &MetricRegistry::GaugeVecHandle::increase);
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tls_certificate_selector_config_decrement_gauge(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  return recordMetric(config_envoy_ptr, id, label_values, label_values_length, value,
                      &MetricRegistry::getGaugeById, &MetricRegistry::getGaugeVecById,
                      &MetricRegistry::GaugeHandle::decrease,
                      &MetricRegistry::GaugeVecHandle::decrease);
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tls_certificate_selector_config_define_histogram(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr) {
  OptRef<MetricRegistry> metrics = definableMetrics(config_envoy_ptr);
  if (!metrics.has_value()) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  const Stats::StatName stat_name = metrics->statNamePool().add(toStringView(name));
  const Stats::Histogram::Unit unit = Stats::Histogram::Unit::Unspecified;
  if (label_names_length == 0) {
    *histogram_id_ptr = metrics->addHistogram(MetricRegistry::HistogramHandle(
        Stats::Utility::histogramFromStatNames(metrics->scope(), {stat_name}, unit)));
  } else {
    *histogram_id_ptr = metrics->addHistogramVec(MetricRegistry::HistogramVecHandle(
        stat_name, labelNames(*metrics, label_names, label_names_length), unit));
  }
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tls_certificate_selector_config_record_histogram_value(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  return recordMetric(config_envoy_ptr, id, label_values, label_values_length, value,
                      &MetricRegistry::getHistogramById, &MetricRegistry::getHistogramVecById,
                      &MetricRegistry::HistogramHandle::recordValue,
                      &MetricRegistry::HistogramVecHandle::recordValue);
}

envoy_dynamic_module_type_tls_certificate_selector_config_scheduler_module_ptr
envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_new(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr) {
  return new DynamicModuleTlsCertificateSelectorConfigScheduler(
      toConfig(config_envoy_ptr)->weak_from_this());
}

void envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_commit(
    envoy_dynamic_module_type_tls_certificate_selector_config_scheduler_module_ptr
        scheduler_module_ptr,
    uint64_t event_id) {
  static_cast<DynamicModuleTlsCertificateSelectorConfigScheduler*>(scheduler_module_ptr)
      ->commit(event_id);
}

void envoy_dynamic_module_callback_tls_certificate_selector_config_scheduler_delete(
    envoy_dynamic_module_type_tls_certificate_selector_config_scheduler_module_ptr
        scheduler_module_ptr) {
  delete static_cast<DynamicModuleTlsCertificateSelectorConfigScheduler*>(scheduler_module_ptr);
}

} // extern "C"

} // namespace DynamicModules
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
