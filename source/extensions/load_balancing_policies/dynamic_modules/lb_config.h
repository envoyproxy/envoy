#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/common/logger.h"
#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {

// The default custom stat namespace which prepends all user-defined metrics.
// This can be overridden via the ``metrics_namespace`` field in ``DynamicModuleConfig``.
constexpr absl::string_view DefaultMetricsNamespace = "dynamicmodulescustom";

class DynamicModuleLbConfig;
using DynamicModuleLbConfigSharedPtr = std::shared_ptr<DynamicModuleLbConfig>;

/**
 * Function pointer types for the load balancer ABI functions.
 */
using OnLbConfigNewType = decltype(&envoy_dynamic_module_on_lb_config_new);
using OnLbConfigDestroyType = decltype(&envoy_dynamic_module_on_lb_config_destroy);
using OnLbNewType = decltype(&envoy_dynamic_module_on_lb_new);
using OnLbChooseHostType = decltype(&envoy_dynamic_module_on_lb_choose_host);
using OnLbOnHostMembershipUpdateType =
    decltype(&envoy_dynamic_module_on_lb_on_host_membership_update);
using OnLbDestroyType = decltype(&envoy_dynamic_module_on_lb_destroy);

/**
 * Configuration for a dynamic module load balancer. This holds the loaded dynamic module and
 * the resolved function pointers for the ABI.
 */
class DynamicModuleLbConfig : public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  /**
   * Creates a new DynamicModuleLbConfig.
   *
   * @param lb_policy_name the name identifying the load balancer implementation in the module.
   * @param lb_config the configuration bytes to pass to the module.
   * @param metrics_namespace the namespace prefix for metrics emitted by this module.
   * @param dynamic_module the loaded dynamic module.
   * @param stats_scope the stats scope for creating custom metrics.
   * @return a shared pointer to the config, or an error status.
   */
  static absl::StatusOr<DynamicModuleLbConfigSharedPtr>
  create(const std::string& lb_policy_name, const std::string& lb_config,
         const std::string& metrics_namespace,
         Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module,
         Stats::Scope& stats_scope);

  ~DynamicModuleLbConfig();

  // Function pointers resolved from the dynamic module.
  OnLbConfigNewType on_config_new_;
  OnLbConfigDestroyType on_config_destroy_;
  OnLbNewType on_lb_new_;
  OnLbChooseHostType on_choose_host_;
  OnLbOnHostMembershipUpdateType on_host_membership_update_;
  OnLbDestroyType on_lb_destroy_;

  // The in-module configuration pointer.
  envoy_dynamic_module_type_lb_config_module_ptr in_module_config_{nullptr};

  // ----------------------------- Metrics Support -----------------------------

  class ModuleCounterHandle {
  public:
    ModuleCounterHandle(Stats::Counter& counter) : counter_(counter) {}
    void add(uint64_t value) const { counter_.add(value); }

  private:
    Stats::Counter& counter_;
  };

  class ModuleCounterVecHandle {
  public:
    ModuleCounterVecHandle(Stats::StatName name, Stats::StatNameVec label_names)
        : name_(name), label_names_(label_names) {}
    const Stats::StatNameVec& getLabelNames() const { return label_names_; }
    void add(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::counterFromElements(scope, {name_}, tags).add(amount);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
  };

  class ModuleGaugeHandle {
  public:
    ModuleGaugeHandle(Stats::Gauge& gauge) : gauge_(gauge) {}
    void add(uint64_t value) const { gauge_.add(value); }
    void sub(uint64_t value) const { gauge_.sub(value); }
    void set(uint64_t value) const { gauge_.set(value); }

  private:
    Stats::Gauge& gauge_;
  };

  class ModuleGaugeVecHandle {
  public:
    ModuleGaugeVecHandle(Stats::StatName name, Stats::StatNameVec label_names,
                         Stats::Gauge::ImportMode import_mode)
        : name_(name), label_names_(label_names), import_mode_(import_mode) {}
    const Stats::StatNameVec& getLabelNames() const { return label_names_; }
    void add(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).add(amount);
    }
    void sub(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).sub(amount);
    }
    void set(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).set(amount);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
    Stats::Gauge::ImportMode import_mode_;
  };

  class ModuleHistogramHandle {
  public:
    ModuleHistogramHandle(Stats::Histogram& histogram) : histogram_(histogram) {}
    void recordValue(uint64_t value) const { histogram_.recordValue(value); }

  private:
    Stats::Histogram& histogram_;
  };

  class ModuleHistogramVecHandle {
  public:
    ModuleHistogramVecHandle(Stats::StatName name, Stats::StatNameVec label_names,
                             Stats::Histogram::Unit unit)
        : name_(name), label_names_(label_names), unit_(unit) {}
    const Stats::StatNameVec& getLabelNames() const { return label_names_; }
    void recordValue(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                     uint64_t value) const {
      ASSERT(tags.has_value());
      Stats::Utility::histogramFromElements(scope, {name_}, unit_, tags).recordValue(value);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
    Stats::Histogram::Unit unit_;
  };

// We use 1-based IDs for the metrics in the ABI, so we need to convert them to 0-based indices
// for our internal storage. These helper functions do that conversion.
#define ID_TO_INDEX(id) ((id) - 1)

  size_t addCounter(ModuleCounterHandle&& counter) {
    counters_.push_back(std::move(counter));
    return counters_.size();
  }
  size_t addCounterVec(ModuleCounterVecHandle&& counter) {
    counter_vecs_.push_back(std::move(counter));
    return counter_vecs_.size();
  }

  size_t addGauge(ModuleGaugeHandle&& gauge) {
    gauges_.push_back(std::move(gauge));
    return gauges_.size();
  }
  size_t addGaugeVec(ModuleGaugeVecHandle&& gauge) {
    gauge_vecs_.push_back(std::move(gauge));
    return gauge_vecs_.size();
  }

  size_t addHistogram(ModuleHistogramHandle&& histogram) {
    histograms_.push_back(std::move(histogram));
    return histograms_.size();
  }
  size_t addHistogramVec(ModuleHistogramVecHandle&& histogram) {
    histogram_vecs_.push_back(std::move(histogram));
    return histogram_vecs_.size();
  }

  OptRef<const ModuleCounterHandle> getCounterById(size_t id) const {
    if (id == 0 || id > counters_.size()) {
      return {};
    }
    return counters_[ID_TO_INDEX(id)];
  }
  OptRef<const ModuleCounterVecHandle> getCounterVecById(size_t id) const {
    if (id == 0 || id > counter_vecs_.size()) {
      return {};
    }
    return counter_vecs_[ID_TO_INDEX(id)];
  }

  OptRef<const ModuleGaugeHandle> getGaugeById(size_t id) const {
    if (id == 0 || id > gauges_.size()) {
      return {};
    }
    return gauges_[ID_TO_INDEX(id)];
  }
  OptRef<const ModuleGaugeVecHandle> getGaugeVecById(size_t id) const {
    if (id == 0 || id > gauge_vecs_.size()) {
      return {};
    }
    return gauge_vecs_[ID_TO_INDEX(id)];
  }

  OptRef<const ModuleHistogramHandle> getHistogramById(size_t id) const {
    if (id == 0 || id > histograms_.size()) {
      return {};
    }
    return histograms_[ID_TO_INDEX(id)];
  }
  OptRef<const ModuleHistogramVecHandle> getHistogramVecById(size_t id) const {
    if (id == 0 || id > histogram_vecs_.size()) {
      return {};
    }
    return histogram_vecs_[ID_TO_INDEX(id)];
  }

#undef ID_TO_INDEX

  const Stats::ScopeSharedPtr stats_scope_;
  Stats::StatNamePool stat_name_pool_;
  // We only allow the module to create stats during on_lb_config_new, and not later from worker
  // threads, so that we don't have to wrap stat_name_pool_ in a lock. Per-request label values
  // use a stack-local Stats::StatNameDynamicPool in the increment callbacks (see abi_impl.cc).
  bool stat_creation_frozen_ = false;

private:
  DynamicModuleLbConfig(const std::string& lb_policy_name, const std::string& lb_config,
                        const std::string& metrics_namespace,
                        Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                        Stats::Scope& stats_scope);

  const std::string lb_policy_name_;
  const std::string lb_config_;
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;

  std::vector<ModuleCounterHandle> counters_;
  std::vector<ModuleCounterVecHandle> counter_vecs_;
  std::vector<ModuleGaugeHandle> gauges_;
  std::vector<ModuleGaugeVecHandle> gauge_vecs_;
  std::vector<ModuleHistogramHandle> histograms_;
  std::vector<ModuleHistogramVecHandle> histogram_vecs_;
};

} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
