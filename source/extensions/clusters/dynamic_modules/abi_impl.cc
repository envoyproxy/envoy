// NOLINT(namespace-envoy)

// This file provides host-side implementations for the cluster dynamic module ABI callbacks.

#include "source/common/common/assert.h"
#include "source/extensions/clusters/dynamic_modules/cluster.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

namespace {

Envoy::Extensions::Clusters::DynamicModules::DynamicModuleCluster*
getCluster(envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  return static_cast<Envoy::Extensions::Clusters::DynamicModules::DynamicModuleCluster*>(
      cluster_envoy_ptr);
}

Envoy::Extensions::Clusters::DynamicModules::DynamicModuleLoadBalancer*
getLb(envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr) {
  return static_cast<Envoy::Extensions::Clusters::DynamicModules::DynamicModuleLoadBalancer*>(
      lb_envoy_ptr);
}

Envoy::Extensions::Clusters::DynamicModules::DynamicModuleClusterConfig*
getConfig(envoy_dynamic_module_type_cluster_config_envoy_ptr config_envoy_ptr) {
  return static_cast<Envoy::Extensions::Clusters::DynamicModules::DynamicModuleClusterConfig*>(
      config_envoy_ptr);
}

Envoy::Stats::StatNameTagVector buildTagsForClusterMetric(
    Envoy::Extensions::Clusters::DynamicModules::DynamicModuleClusterConfig& config,
    const Envoy::Stats::StatNameVec& label_names,
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

bool envoy_dynamic_module_callback_cluster_add_hosts(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    const envoy_dynamic_module_type_module_buffer* addresses, const uint32_t* weights, size_t count,
    envoy_dynamic_module_type_cluster_host_envoy_ptr* result_host_ptrs) {
  auto* cluster = getCluster(cluster_envoy_ptr);
  std::vector<std::string> address_strings;
  address_strings.reserve(count);
  std::vector<uint32_t> weight_vec(weights, weights + count);
  for (size_t i = 0; i < count; ++i) {
    address_strings.emplace_back(addresses[i].ptr, addresses[i].length);
  }
  std::vector<Envoy::Upstream::HostSharedPtr> result_hosts;
  if (!cluster->addHosts(address_strings, weight_vec, result_hosts)) {
    return false;
  }
  for (size_t i = 0; i < result_hosts.size(); ++i) {
    result_host_ptrs[i] = const_cast<Envoy::Upstream::Host*>(result_hosts[i].get());
  }
  return true;
}

size_t envoy_dynamic_module_callback_cluster_remove_hosts(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    const envoy_dynamic_module_type_cluster_host_envoy_ptr* host_envoy_ptrs, size_t count) {
  auto* cluster = getCluster(cluster_envoy_ptr);
  std::vector<Envoy::Upstream::HostSharedPtr> hosts;
  hosts.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    hosts.emplace_back(cluster->findHost(host_envoy_ptrs[i]));
  }
  return cluster->removeHosts(hosts);
}

void envoy_dynamic_module_callback_cluster_pre_init_complete(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  getCluster(cluster_envoy_ptr)->preInitComplete();
}

size_t envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority) {
  auto* lb = getLb(lb_envoy_ptr);
  const auto& priority_set = lb->prioritySet();
  if (priority >= priority_set.hostSetsPerPriority().size()) {
    return 0;
  }
  return priority_set.hostSetsPerPriority()[priority]->healthyHosts().size();
}

envoy_dynamic_module_type_cluster_host_envoy_ptr
envoy_dynamic_module_callback_cluster_lb_get_healthy_host(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index) {
  auto* lb = getLb(lb_envoy_ptr);
  const auto& priority_set = lb->prioritySet();
  if (priority >= priority_set.hostSetsPerPriority().size()) {
    return nullptr;
  }
  const auto& healthy_hosts = priority_set.hostSetsPerPriority()[priority]->healthyHosts();
  if (index >= healthy_hosts.size()) {
    return nullptr;
  }
  return const_cast<Envoy::Upstream::Host*>(healthy_hosts[index].get());
}

envoy_dynamic_module_type_cluster_scheduler_module_ptr
envoy_dynamic_module_callback_cluster_scheduler_new(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  return Envoy::Extensions::Clusters::DynamicModules::DynamicModuleClusterScheduler::create(
      getCluster(cluster_envoy_ptr));
}

void envoy_dynamic_module_callback_cluster_scheduler_delete(
    envoy_dynamic_module_type_cluster_scheduler_module_ptr scheduler_module_ptr) {
  delete static_cast<Envoy::Extensions::Clusters::DynamicModules::DynamicModuleClusterScheduler*>(
      scheduler_module_ptr);
}

void envoy_dynamic_module_callback_cluster_scheduler_commit(
    envoy_dynamic_module_type_cluster_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id) {
  auto* scheduler =
      static_cast<Envoy::Extensions::Clusters::DynamicModules::DynamicModuleClusterScheduler*>(
          scheduler_module_ptr);
  scheduler->commit(event_id);
}

// =============================================================================
// Metrics Callbacks
// =============================================================================

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_define_counter(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr) {
  auto* config = getConfig(cluster_config_envoy_ptr);
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
envoy_dynamic_module_callback_cluster_config_increment_counter(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(cluster_config_envoy_ptr);

  if (label_values_length == 0) {
    auto counter = config->getCounterById(id);
    if (!counter.has_value()) {
      if (config->getCounterVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
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
  auto tags = buildTagsForClusterMetric(*config, counter->getLabelNames(), label_values,
                                        label_values_length);
  counter->add(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_cluster_config_define_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr) {
  auto* config = getConfig(cluster_config_envoy_ptr);
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

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_cluster_config_set_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(cluster_config_envoy_ptr);

  if (label_values_length == 0) {
    auto gauge = config->getGaugeById(id);
    if (!gauge.has_value()) {
      if (config->getGaugeVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
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
  auto tags =
      buildTagsForClusterMetric(*config, gauge->getLabelNames(), label_values, label_values_length);
  gauge->set(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_increment_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(cluster_config_envoy_ptr);

  if (label_values_length == 0) {
    auto gauge = config->getGaugeById(id);
    if (!gauge.has_value()) {
      if (config->getGaugeVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
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
  auto tags =
      buildTagsForClusterMetric(*config, gauge->getLabelNames(), label_values, label_values_length);
  gauge->add(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_decrement_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(cluster_config_envoy_ptr);

  if (label_values_length == 0) {
    auto gauge = config->getGaugeById(id);
    if (!gauge.has_value()) {
      if (config->getGaugeVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
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
  auto tags =
      buildTagsForClusterMetric(*config, gauge->getLabelNames(), label_values, label_values_length);
  gauge->sub(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_define_histogram(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr) {
  auto* config = getConfig(cluster_config_envoy_ptr);
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
envoy_dynamic_module_callback_cluster_config_record_histogram_value(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(cluster_config_envoy_ptr);

  if (label_values_length == 0) {
    auto histogram = config->getHistogramById(id);
    if (!histogram.has_value()) {
      if (config->getHistogramVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
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
  auto tags = buildTagsForClusterMetric(*config, histogram->getLabelNames(), label_values,
                                        label_values_length);
  histogram->recordValue(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

} // extern "C"
