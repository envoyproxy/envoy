#pragma once

#include "envoy/common/optref.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/tracing/trace_driver.h"

#include "source/common/common/statusor.h"
#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace DynamicModules {

// Type aliases for function pointers resolved from the module.
using OnTracerConfigNewType = decltype(&envoy_dynamic_module_on_tracer_config_new);
using OnTracerConfigDestroyType = decltype(&envoy_dynamic_module_on_tracer_config_destroy);
using OnTracerStartSpanType = decltype(&envoy_dynamic_module_on_tracer_start_span);
using OnTracerSpanSetOperationType = decltype(&envoy_dynamic_module_on_tracer_span_set_operation);
using OnTracerSpanSetTagType = decltype(&envoy_dynamic_module_on_tracer_span_set_tag);
using OnTracerSpanLogType = decltype(&envoy_dynamic_module_on_tracer_span_log);
using OnTracerSpanFinishType = decltype(&envoy_dynamic_module_on_tracer_span_finish);
using OnTracerSpanInjectContextType = decltype(&envoy_dynamic_module_on_tracer_span_inject_context);
using OnTracerSpanSpawnChildType = decltype(&envoy_dynamic_module_on_tracer_span_spawn_child);
using OnTracerSpanSetSampledType = decltype(&envoy_dynamic_module_on_tracer_span_set_sampled);
using OnTracerSpanUseLocalDecisionType =
    decltype(&envoy_dynamic_module_on_tracer_span_use_local_decision);
using OnTracerSpanGetBaggageType = decltype(&envoy_dynamic_module_on_tracer_span_get_baggage);
using OnTracerSpanSetBaggageType = decltype(&envoy_dynamic_module_on_tracer_span_set_baggage);
using OnTracerSpanGetTraceIdType = decltype(&envoy_dynamic_module_on_tracer_span_get_trace_id);
using OnTracerSpanGetSpanIdType = decltype(&envoy_dynamic_module_on_tracer_span_get_span_id);
using OnTracerSpanDestroyType = decltype(&envoy_dynamic_module_on_tracer_span_destroy);

// The default custom stat namespace which prepends all user-defined metrics.
// This can be overridden via the ``metrics_namespace`` field in ``DynamicModuleConfig``.
constexpr absl::string_view DefaultMetricsNamespace = "dynamicmodulescustom";

/**
 * Configuration for dynamic module tracers. This resolves and holds the symbols used for
 * tracing. Multiple driver/span instances may share this config.
 *
 * Note: Symbol resolution and in-module config creation are done in the factory function
 * newDynamicModuleTracerConfig() to provide graceful error handling. The constructor
 * only initializes basic members.
 */
class DynamicModuleTracerConfig {
public:
  DynamicModuleTracerConfig(const absl::string_view tracer_name,
                            const absl::string_view tracer_config,
                            const absl::string_view metrics_namespace,
                            Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                            Stats::Scope& stats_scope);

  ~DynamicModuleTracerConfig();

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_tracer_config_module_ptr in_module_config_{nullptr};

  // Function pointers for the module. All required ones are resolved during
  // newDynamicModuleTracerConfig() and guaranteed non-nullptr after that.
  OnTracerConfigDestroyType on_config_destroy_{nullptr};
  OnTracerStartSpanType on_start_span_{nullptr};
  OnTracerSpanSetOperationType on_span_set_operation_{nullptr};
  OnTracerSpanSetTagType on_span_set_tag_{nullptr};
  OnTracerSpanLogType on_span_log_{nullptr};
  OnTracerSpanFinishType on_span_finish_{nullptr};
  OnTracerSpanInjectContextType on_span_inject_context_{nullptr};
  OnTracerSpanSpawnChildType on_span_spawn_child_{nullptr};
  OnTracerSpanSetSampledType on_span_set_sampled_{nullptr};
  OnTracerSpanUseLocalDecisionType on_span_use_local_decision_{nullptr};
  OnTracerSpanGetBaggageType on_span_get_baggage_{nullptr};
  OnTracerSpanSetBaggageType on_span_set_baggage_{nullptr};
  OnTracerSpanGetTraceIdType on_span_get_trace_id_{nullptr};
  OnTracerSpanGetSpanIdType on_span_get_span_id_{nullptr};
  OnTracerSpanDestroyType on_span_destroy_{nullptr};

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

  size_t addCounter(ModuleCounterHandle&& counter) {
    counters_.push_back(std::move(counter));
    return counters_.size();
  }

  size_t addCounterVec(ModuleCounterVecHandle&& counter_vec) {
    counter_vecs_.push_back(std::move(counter_vec));
    return counter_vecs_.size();
  }

  size_t addGauge(ModuleGaugeHandle&& gauge) {
    gauges_.push_back(std::move(gauge));
    return gauges_.size();
  }

  size_t addGaugeVec(ModuleGaugeVecHandle&& gauge_vec) {
    gauge_vecs_.push_back(std::move(gauge_vec));
    return gauge_vecs_.size();
  }

  size_t addHistogram(ModuleHistogramHandle&& histogram) {
    histograms_.push_back(std::move(histogram));
    return histograms_.size();
  }

  size_t addHistogramVec(ModuleHistogramVecHandle&& histogram_vec) {
    histogram_vecs_.push_back(std::move(histogram_vec));
    return histogram_vecs_.size();
  }

  OptRef<const ModuleCounterHandle> getCounterById(size_t id) const {
    if (id == 0 || id > counters_.size()) {
      return {};
    }
    return counters_[id - 1];
  }

  OptRef<const ModuleCounterVecHandle> getCounterVecById(size_t id) const {
    if (id == 0 || id > counter_vecs_.size()) {
      return {};
    }
    return counter_vecs_[id - 1];
  }

  OptRef<const ModuleGaugeHandle> getGaugeById(size_t id) const {
    if (id == 0 || id > gauges_.size()) {
      return {};
    }
    return gauges_[id - 1];
  }

  OptRef<const ModuleGaugeVecHandle> getGaugeVecById(size_t id) const {
    if (id == 0 || id > gauge_vecs_.size()) {
      return {};
    }
    return gauge_vecs_[id - 1];
  }

  OptRef<const ModuleHistogramHandle> getHistogramById(size_t id) const {
    if (id == 0 || id > histograms_.size()) {
      return {};
    }
    return histograms_[id - 1];
  }

  OptRef<const ModuleHistogramVecHandle> getHistogramVecById(size_t id) const {
    if (id == 0 || id > histogram_vecs_.size()) {
      return {};
    }
    return histogram_vecs_[id - 1];
  }

  const Stats::ScopeSharedPtr stats_scope_;
  Stats::StatNamePool stat_name_pool_;

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleTracerConfig>> newDynamicModuleTracerConfig(
      const absl::string_view tracer_name, const absl::string_view tracer_config,
      const absl::string_view metrics_namespace,
      Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope);

  const std::string tracer_name_;
  const std::string tracer_config_;
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
  std::vector<ModuleCounterHandle> counters_;
  std::vector<ModuleCounterVecHandle> counter_vecs_;
  std::vector<ModuleGaugeHandle> gauges_;
  std::vector<ModuleGaugeVecHandle> gauge_vecs_;
  std::vector<ModuleHistogramHandle> histograms_;
  std::vector<ModuleHistogramVecHandle> histogram_vecs_;
};

using DynamicModuleTracerConfigSharedPtr = std::shared_ptr<DynamicModuleTracerConfig>;

/**
 * Creates a new DynamicModuleTracerConfig for the given configuration.
 * @param tracer_name the name of the tracer.
 * @param tracer_config the configuration bytes for the tracer.
 * @param metrics_namespace the namespace prefix for metrics emitted by this module.
 * @param dynamic_module the dynamic module to use.
 * @param stats_scope the stats scope for metrics.
 * @return a shared pointer to the new config object or an error if symbol resolution failed.
 */
absl::StatusOr<DynamicModuleTracerConfigSharedPtr> newDynamicModuleTracerConfig(
    const absl::string_view tracer_name, const absl::string_view tracer_config,
    const absl::string_view metrics_namespace,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope);

/**
 * DynamicModuleSpan wraps an in-module span and implements the Tracing::Span interface.
 * It holds a mutable TraceContext* that is updated to point to the currently active trace
 * context during startSpan (incoming) and injectContext (outgoing).
 */
class DynamicModuleSpan : public Tracing::Span {
public:
  DynamicModuleSpan(DynamicModuleTracerConfigSharedPtr config,
                    envoy_dynamic_module_type_tracer_span_module_ptr in_module_span,
                    Tracing::TraceContext* trace_context);
  ~DynamicModuleSpan() override;

  // Tracing::Span interface.
  void setOperation(absl::string_view operation) override;
  void setTag(absl::string_view name, absl::string_view value) override;
  void log(SystemTime timestamp, const std::string& event) override;
  void finishSpan() override;
  void injectContext(Tracing::TraceContext& trace_context,
                     const Tracing::UpstreamContext& upstream) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;
  void setSampled(bool sampled) override;
  bool useLocalDecision() const override;
  std::string getBaggage(absl::string_view key) override;
  void setBaggage(absl::string_view key, absl::string_view value) override;
  std::string getTraceId() const override;
  std::string getSpanId() const override;

  // Returns the currently active trace context for callback implementations.
  Tracing::TraceContext* traceContext() { return trace_context_; }

private:
  friend class DynamicModuleDriver;

  DynamicModuleTracerConfigSharedPtr config_;
  envoy_dynamic_module_type_tracer_span_module_ptr in_module_span_;
  Tracing::TraceContext* trace_context_;
};

/**
 * DynamicModuleDriver implements the Tracing::Driver interface and delegates span creation
 * to the dynamic module.
 */
class DynamicModuleDriver : public Tracing::Driver {
public:
  explicit DynamicModuleDriver(DynamicModuleTracerConfigSharedPtr config);

  // Tracing::Driver interface.
  Tracing::SpanPtr startSpan(const Tracing::Config& config, Tracing::TraceContext& trace_context,
                             const StreamInfo::StreamInfo& stream_info,
                             const std::string& operation_name,
                             Tracing::Decision tracing_decision) override;

private:
  DynamicModuleTracerConfigSharedPtr config_;
};

} // namespace DynamicModules
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
