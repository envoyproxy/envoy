#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/common/regex.h"
#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/http/orca_reporter/v3/orca_reporter.pb.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/http/filter.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/manager.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/orca_reporter/cgroup_stats.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OrcaReporter {

using ProtoConfig = envoy::extensions::filters::http::orca_reporter::v3::OrcaReporter;

// Computes the value of one metric from cgroup stats, once per refresh.
class MetricSampler {
public:
  virtual ~MetricSampler() = default;

  // Returns the current utilization, or nullopt if it is unavailable.
  virtual std::optional<double> sample(MonotonicTime now) PURE;
};
using MetricSamplerPtr = std::unique_ptr<MetricSampler>;

// CPU utilization: growth of usage_usec over the elapsed time times the capacity.
class CpuSampler : public MetricSampler, Logger::Loggable<Logger::Id::filter> {
public:
  CpuSampler(std::vector<CgroupReader> cgroups, std::optional<double> capacity_cores)
      : cgroups_(std::move(cgroups)), capacity_cores_(capacity_cores) {}

  std::optional<double> sample(MonotonicTime now) override;

private:
  std::optional<double> capacity() const;

  const std::vector<CgroupReader> cgroups_;
  const std::optional<double> capacity_cores_;
  std::optional<MonotonicTime> last_time_;
  std::vector<uint64_t> last_usage_;
  std::optional<double> last_value_;
};

// Memory utilization: working set over the capacity.
class MemorySampler : public MetricSampler, Logger::Loggable<Logger::Id::filter> {
public:
  MemorySampler(std::vector<CgroupReader> cgroups, std::optional<uint64_t> capacity_bytes)
      : cgroups_(std::move(cgroups)), capacity_bytes_(capacity_bytes) {}

  std::optional<double> sample(MonotonicTime now) override;

private:
  std::optional<uint64_t> capacity() const;

  const std::vector<CgroupReader> cgroups_;
  const std::optional<uint64_t> capacity_bytes_;
};

// The ORCA field a metric is written to.
struct MetricTarget {
  enum class Kind {
    CpuUtilization,
    MemUtilization,
    ApplicationUtilization,
    NamedMetric,
    Utilization,
  };

  Kind kind;
  // The map key for NamedMetric and Utilization.
  std::string key;
};

// Parses a metric name such as "cpu_utilization" or "named_metrics.foo".
absl::StatusOr<MetricTarget> parseMetricName(absl::string_view name);

// Samples the configured metrics on the main thread and publishes the encoded report to the
// workers. Shared by every filter config with the same metrics, format and refresh interval.
class LoadReporter : Logger::Loggable<Logger::Id::filter> {
public:
  // The returned pointer is always destroyed on the main thread, because the reporter owns a
  // main thread timer and the last reference may be dropped by a stream on a worker.
  static absl::StatusOr<std::shared_ptr<LoadReporter>>
  create(const ProtoConfig& proto_config, Filesystem::Instance& fs, Regex::Engine& regex_engine,
         TimeSource& time_source, Event::Dispatcher& main_dispatcher,
         ThreadLocal::SlotAllocator& tls, const EnvLookup& env_lookup);

  // The header the report is written to.
  const Http::LowerCaseString& headerName() const { return header_name_; }

  // The encoded report on the current thread. Empty if no metric is available.
  const std::string& encodedReport() const { return (*tls_)->encoded_report; }

  // Called on the workers for every request. The count is turned into rps_fractional on the next
  // refresh.
  void onRequest() { requests_.fetch_add(1, std::memory_order_relaxed); }

private:
  struct Metric {
    std::string name;
    MetricTarget target;
    MetricSamplerPtr sampler;
  };

  struct ThreadLocalReport : public ThreadLocal::ThreadLocalObject {
    explicit ThreadLocalReport(std::string report) : encoded_report(std::move(report)) {}
    std::string encoded_report;
  };

  LoadReporter(const ProtoConfig& proto_config, std::vector<Metric> metrics,
               TimeSource& time_source, Event::Dispatcher& main_dispatcher,
               ThreadLocal::SlotAllocator& tls);

  // Samples all metrics and returns the encoded report.
  std::string sampleAndEncode();
  void updateRequestRate(MonotonicTime now);
  void onRefreshTimer();

  const ProtoConfig::Format format_;
  const Http::LowerCaseString& header_name_;
  const std::chrono::milliseconds refresh_interval_;
  std::vector<Metric> metrics_;
  TimeSource& time_source_;
  // Only accessed on the main thread.
  std::string published_report_;
  std::atomic<uint64_t> requests_{0};
  // Main thread only.
  std::optional<MonotonicTime> last_rate_time_;
  uint64_t last_requests_{0};
  double rps_{0};
  ThreadLocal::TypedSlotPtr<ThreadLocalReport> tls_;
  Event::TimerPtr refresh_timer_;
};
using LoadReporterSharedPtr = std::shared_ptr<LoadReporter>;

// Hands out one LoadReporter per distinct sampling config, so that listeners with the same config
// sample once, and a reload that does not change the config keeps the CPU baseline. Only used on
// the main thread.
class LoadReporterRegistry : public Singleton::Instance {
public:
  static std::shared_ptr<LoadReporterRegistry> get(Singleton::Manager& singleton_manager);

  absl::StatusOr<LoadReporterSharedPtr>
  getOrCreate(const ProtoConfig& proto_config, Filesystem::Instance& fs,
              Regex::Engine& regex_engine, TimeSource& time_source,
              Event::Dispatcher& main_dispatcher, ThreadLocal::SlotAllocator& tls,
              const EnvLookup& env_lookup);

private:
  // Keyed by the deterministic serialization of the sampling part of the config.
  absl::flat_hash_map<std::string, std::weak_ptr<LoadReporter>> reporters_;
};

class OrcaReporterConfig {
public:
  static absl::StatusOr<std::shared_ptr<OrcaReporterConfig>>
  create(const ProtoConfig& proto_config, Server::Configuration::ServerFactoryContext& context);

  OrcaReporterConfig(const ProtoConfig& proto_config, LoadReporterSharedPtr reporter)
      : overwrite_existing_report_(proto_config.overwrite_existing_report()),
        reporter_(std::move(reporter)) {}

  bool overwriteExistingReport() const { return overwrite_existing_report_; }
  LoadReporter& reporter() const { return *reporter_; }

private:
  const bool overwrite_existing_report_;
  const LoadReporterSharedPtr reporter_;
};
using OrcaReporterConfigSharedPtr = std::shared_ptr<OrcaReporterConfig>;

// Adds the latest load report to the headers and the trailers of every response.
class OrcaReporterFilter : public Http::PassThroughFilter {
public:
  explicit OrcaReporterFilter(OrcaReporterConfigSharedPtr config) : config_(std::move(config)) {}

  // Http::StreamFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

private:
  // Adds the report to the response headers or trailers. Envoy clients use the first report they
  // see and gRPC clients read the trailers, so the report is written to both.
  void writeReport(Http::HeaderMap& headers) const;

  const OrcaReporterConfigSharedPtr config_;
};

} // namespace OrcaReporter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
