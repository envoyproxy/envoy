#include "source/extensions/filters/http/orca_reporter/orca_reporter.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "source/common/common/base64.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"
#include "source/common/orca/orca_parser.h"
#include "source/common/protobuf/utility.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/strip.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OrcaReporter {

namespace {

constexpr std::chrono::milliseconds kDefaultRefreshInterval{1000};
// The first refresh runs sooner so that CPU utilization is reported soon after creation.
constexpr std::chrono::milliseconds kFirstRefreshInterval{100};

const Http::LowerCaseString& textHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, Orca::kEndpointLoadMetricsHeader);
}

const Http::LowerCaseString& binaryHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, Orca::kEndpointLoadMetricsHeaderBin);
}

void setMetric(const MetricTarget& target, double value,
               xds::data::orca::v3::OrcaLoadReport& report) {
  switch (target.kind) {
  case MetricTarget::Kind::CpuUtilization:
    report.set_cpu_utilization(value);
    return;
  case MetricTarget::Kind::MemUtilization:
    report.set_mem_utilization(value);
    return;
  case MetricTarget::Kind::ApplicationUtilization:
    report.set_application_utilization(value);
    return;
  case MetricTarget::Kind::NamedMetric:
    (*report.mutable_named_metrics())[target.key] = value;
    return;
  case MetricTarget::Kind::Utilization:
    (*report.mutable_utilization())[target.key] = value;
    return;
  }
}

absl::StatusOr<std::vector<CgroupReader>>
createReaders(const Protobuf::RepeatedPtrField<
                  envoy::extensions::filters::http::orca_reporter::v3::CgroupSource>& sources,
              Filesystem::Instance& fs, Regex::Engine& regex_engine, const EnvLookup& env_lookup) {
  std::vector<CgroupReader> readers;
  readers.reserve(sources.size());
  for (const auto& source : sources) {
    std::vector<Matcher::RegexReplace> rewrites;
    rewrites.reserve(source.value_rewrites_size());
    for (const auto& rewrite : source.value_rewrites()) {
      absl::StatusOr<Matcher::RegexReplace> compiled =
          Matcher::RegexReplace::create(regex_engine, rewrite);
      RETURN_IF_NOT_OK_REF(compiled.status());
      rewrites.push_back(std::move(compiled).value());
    }

    absl::StatusOr<std::string> path = resolvePathTemplate(source.path(), rewrites, env_lookup);
    RETURN_IF_NOT_OK_REF(path.status());
    if (!fs.directoryExists(path.value())) {
      return absl::InvalidArgumentError(
          fmt::format("cgroup directory '{}' does not exist", path.value()));
    }
    readers.emplace_back(fs, std::move(path).value());
  }
  return readers;
}

// Keys are written as-is into the TEXT format, which separates entries with ',' and keys from
// values with '='. Restricting the characters keeps every format parseable.
bool validMetricKey(absl::string_view key) {
  return !key.empty() && std::all_of(key.begin(), key.end(), [](char c) {
    return absl::ascii_isalnum(c) || c == '_' || c == '.' || c == '-';
  });
}

absl::StatusOr<MetricTarget> metricWithKey(MetricTarget::Kind kind, absl::string_view name,
                                           absl::string_view key) {
  if (!validMetricKey(key)) {
    return absl::InvalidArgumentError(fmt::format(
        "invalid ORCA metric name '{}': the key may only contain [A-Za-z0-9_.-]", name));
  }
  return MetricTarget{kind, std::string(key)};
}

} // namespace

std::optional<double> CpuSampler::capacity() const {
  if (capacity_cores_.has_value()) {
    return capacity_cores_;
  }
  double total = 0;
  for (const CgroupReader& cgroup : cgroups_) {
    absl::StatusOr<std::optional<double>> limit = cgroup.cpuLimitCores();
    if (!limit.ok()) {
      ENVOY_LOG(debug, "orca_reporter: {}", limit.status().message());
      return std::nullopt;
    }
    if (!limit->has_value()) {
      ENVOY_LOG(debug, "orca_reporter: cgroup {} has no CPU limit", cgroup.directory());
      return std::nullopt;
    }
    total += limit->value();
  }
  return total;
}

std::optional<double> CpuSampler::sample(MonotonicTime now) {
  std::vector<uint64_t> usage;
  usage.reserve(cgroups_.size());
  for (const CgroupReader& cgroup : cgroups_) {
    absl::StatusOr<uint64_t> value = cgroup.cpuUsageUsec();
    if (!value.ok()) {
      ENVOY_LOG(debug, "orca_reporter: {}", value.status().message());
      // Start over from the next successful sample.
      last_time_.reset();
      last_value_.reset();
      return std::nullopt;
    }
    usage.push_back(value.value());
  }

  if (!last_time_.has_value()) {
    // The first sample only sets the baseline.
    last_time_ = now;
    last_usage_ = std::move(usage);
    return std::nullopt;
  }
  const auto elapsed_usec =
      std::chrono::duration_cast<std::chrono::microseconds>(now - *last_time_).count();
  // Without elapsed time, keep the baseline so that the usage is counted in the next interval.
  if (elapsed_usec <= 0) {
    return last_value_;
  }
  const std::vector<uint64_t> last_usage = std::exchange(last_usage_, usage);
  last_time_ = now;

  uint64_t used_usec = 0;
  for (size_t i = 0; i < usage.size(); ++i) {
    if (usage[i] < last_usage[i]) {
      // The cgroup was recreated. Report the previous value for this interval; the next one is
      // computed from the new baseline.
      return last_value_;
    }
    used_usec += usage[i] - last_usage[i];
  }

  const std::optional<double> cores = capacity();
  if (!cores.has_value()) {
    last_value_.reset();
    return std::nullopt;
  }
  last_value_ = static_cast<double>(used_usec) / (static_cast<double>(elapsed_usec) * *cores);
  return last_value_;
}

std::optional<double> MemorySampler::capacity() const {
  if (capacity_bytes_.has_value()) {
    return static_cast<double>(*capacity_bytes_);
  }
  // Summed as double, so that very large limits can't overflow.
  double total = 0;
  for (const CgroupReader& cgroup : cgroups_) {
    absl::StatusOr<std::optional<uint64_t>> limit = cgroup.memoryLimitBytes();
    if (!limit.ok()) {
      ENVOY_LOG(debug, "orca_reporter: {}", limit.status().message());
      return std::nullopt;
    }
    if (!limit->has_value()) {
      ENVOY_LOG(debug, "orca_reporter: cgroup {} has no memory limit", cgroup.directory());
      return std::nullopt;
    }
    total += limit->value();
  }
  return total;
}

std::optional<double> MemorySampler::sample(MonotonicTime) {
  uint64_t used_bytes = 0;
  for (const CgroupReader& cgroup : cgroups_) {
    absl::StatusOr<uint64_t> value = cgroup.memoryWorkingSetBytes();
    if (!value.ok()) {
      ENVOY_LOG(debug, "orca_reporter: {}", value.status().message());
      return std::nullopt;
    }
    used_bytes += value.value();
  }

  const std::optional<double> bytes = capacity();
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return static_cast<double>(used_bytes) / *bytes;
}

absl::StatusOr<MetricTarget> parseMetricName(absl::string_view name) {
  if (name == Orca::kCpuUtilizationField) {
    return MetricTarget{MetricTarget::Kind::CpuUtilization, ""};
  }
  if (name == Orca::kMemUtilizationField) {
    return MetricTarget{MetricTarget::Kind::MemUtilization, ""};
  }
  if (name == Orca::kApplicationUtilizationField) {
    return MetricTarget{MetricTarget::Kind::ApplicationUtilization, ""};
  }
  absl::string_view key = name;
  if (absl::ConsumePrefix(&key, Orca::kNamedMetricsFieldPrefix)) {
    return metricWithKey(MetricTarget::Kind::NamedMetric, name, key);
  }
  key = name;
  if (absl::ConsumePrefix(&key, Orca::kUtilizationPrefix)) {
    return metricWithKey(MetricTarget::Kind::Utilization, name, key);
  }
  return absl::InvalidArgumentError(fmt::format("unsupported ORCA metric name '{}'", name));
}

SINGLETON_MANAGER_REGISTRATION(orca_load_reporter_registry);

absl::StatusOr<OrcaReporterConfigSharedPtr>
OrcaReporterConfig::create(const ProtoConfig& proto_config,
                           Server::Configuration::ServerFactoryContext& context) {
  absl::StatusOr<LoadReporterSharedPtr> reporter =
      LoadReporterRegistry::get(context.singletonManager())
          ->getOrCreate(proto_config, context.api().fileSystem(), context.regexEngine(),
                        context.timeSource(), context.mainThreadDispatcher(), context.threadLocal(),
                        processEnvLookup);
  RETURN_IF_NOT_OK_REF(reporter.status());
  return std::make_shared<OrcaReporterConfig>(proto_config, std::move(reporter).value());
}

std::shared_ptr<LoadReporterRegistry>
LoadReporterRegistry::get(Singleton::Manager& singleton_manager) {
  // Pinned, so that it is destroyed on the main thread at shutdown. It only holds weak references.
  return singleton_manager.getTyped<LoadReporterRegistry>(
      SINGLETON_MANAGER_REGISTERED_NAME(orca_load_reporter_registry),
      [] { return std::make_shared<LoadReporterRegistry>(); }, true);
}

absl::StatusOr<LoadReporterSharedPtr>
LoadReporterRegistry::getOrCreate(const ProtoConfig& proto_config, Filesystem::Instance& fs,
                                  Regex::Engine& regex_engine, TimeSource& time_source,
                                  Event::Dispatcher& main_dispatcher,
                                  ThreadLocal::SlotAllocator& tls, const EnvLookup& env_lookup) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  // overwrite_existing_report only affects the filter, not the sampling.
  ProtoConfig sampling_config = proto_config;
  sampling_config.clear_overwrite_existing_report();
  std::string key = MessageUtil::serializeDeterministically(sampling_config);

  auto it = reporters_.find(key);
  if (it != reporters_.end()) {
    if (LoadReporterSharedPtr reporter = it->second.lock(); reporter != nullptr) {
      return reporter;
    }
  }
  absl::StatusOr<LoadReporterSharedPtr> reporter = LoadReporter::create(
      proto_config, fs, regex_engine, time_source, main_dispatcher, tls, env_lookup);
  RETURN_IF_NOT_OK_REF(reporter.status());
  absl::erase_if(reporters_, [](const auto& entry) { return entry.second.expired(); });
  reporters_[std::move(key)] = reporter.value();
  return reporter;
}

absl::StatusOr<LoadReporterSharedPtr>
LoadReporter::create(const ProtoConfig& proto_config, Filesystem::Instance& fs,
                     Regex::Engine& regex_engine, TimeSource& time_source,
                     Event::Dispatcher& main_dispatcher, ThreadLocal::SlotAllocator& tls,
                     const EnvLookup& env_lookup) {
#ifndef ENVOY_ENABLE_YAML
  if (proto_config.format() == ProtoConfig::JSON) {
    return absl::InvalidArgumentError("JSON format is not supported in this build");
  }
#endif

  std::vector<Metric> metrics;
  absl::flat_hash_set<std::string> names;
  for (const auto& metric : proto_config.metrics()) {
    if (!names.insert(metric.name()).second) {
      return absl::InvalidArgumentError(
          fmt::format("duplicate ORCA metric name '{}'", metric.name()));
    }
    absl::StatusOr<MetricTarget> target = parseMetricName(metric.name());
    RETURN_IF_NOT_OK_REF(target.status());

    MetricSamplerPtr sampler;
    switch (metric.source_case()) {
    case envoy::extensions::filters::http::orca_reporter::v3::Metric::kCgroupCpu: {
      absl::StatusOr<std::vector<CgroupReader>> readers =
          createReaders(metric.cgroup_cpu().cgroups(), fs, regex_engine, env_lookup);
      RETURN_IF_NOT_OK_REF(readers.status());
      std::optional<double> capacity;
      if (metric.cgroup_cpu().has_capacity_cores()) {
        capacity = metric.cgroup_cpu().capacity_cores().value();
        if (!std::isfinite(*capacity)) {
          return absl::InvalidArgumentError("capacity_cores must be finite");
        }
      }
      sampler = std::make_unique<CpuSampler>(std::move(readers).value(), capacity);
      break;
    }
    case envoy::extensions::filters::http::orca_reporter::v3::Metric::kCgroupMemory: {
      absl::StatusOr<std::vector<CgroupReader>> readers =
          createReaders(metric.cgroup_memory().cgroups(), fs, regex_engine, env_lookup);
      RETURN_IF_NOT_OK_REF(readers.status());
      std::optional<uint64_t> capacity;
      if (metric.cgroup_memory().has_capacity_bytes()) {
        capacity = metric.cgroup_memory().capacity_bytes().value();
      }
      sampler = std::make_unique<MemorySampler>(std::move(readers).value(), capacity);
      break;
    }
    case envoy::extensions::filters::http::orca_reporter::v3::Metric::SOURCE_NOT_SET:
      PANIC_DUE_TO_PROTO_UNSET;
    }
    metrics.push_back(Metric{metric.name(), std::move(target).value(), std::move(sampler)});
  }

  return LoadReporterSharedPtr(
      new LoadReporter(proto_config, std::move(metrics), time_source, main_dispatcher, tls),
      [&main_dispatcher](LoadReporter* reporter) {
        if (main_dispatcher.isThreadSafe()) {
          delete reporter;
          return;
        }
        // If the main dispatcher has already exited the post never runs, and the reporter is
        // leaked at shutdown.
        main_dispatcher.post([reporter]() { delete reporter; });
      });
}

LoadReporter::LoadReporter(const ProtoConfig& proto_config, std::vector<Metric> metrics,
                           TimeSource& time_source, Event::Dispatcher& main_dispatcher,
                           ThreadLocal::SlotAllocator& tls)
    : format_(proto_config.format()),
      header_name_(format_ == ProtoConfig::BINARY ? binaryHeader() : textHeader()),
      refresh_interval_(std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(
          proto_config, refresh_interval, kDefaultRefreshInterval.count()))),
      metrics_(std::move(metrics)), time_source_(time_source),
      tls_(ThreadLocal::TypedSlot<ThreadLocalReport>::makeUnique(tls)),
      refresh_timer_(main_dispatcher.createTimer([this]() { onRefreshTimer(); })) {
  publish(sampleAndEncode());
  refresh_timer_->enableTimer(std::min(kFirstRefreshInterval, refresh_interval_));
}

void LoadReporter::updateRequestRate(MonotonicTime now) {
  const uint64_t requests = requests_.load(std::memory_order_relaxed);
  if (last_rate_time_.has_value()) {
    const double elapsed = std::chrono::duration<double>(now - *last_rate_time_).count();
    // Without elapsed time, keep the previous rate and baseline.
    if (elapsed <= 0) {
      return;
    }
    rps_ = (requests - last_requests_) / elapsed;
  }
  last_rate_time_ = now;
  last_requests_ = requests;
}

std::string LoadReporter::sampleAndEncode() {
  const MonotonicTime now = time_source_.monotonicTime();
  updateRequestRate(now);
  xds::data::orca::v3::OrcaLoadReport report;
  std::vector<std::string> text_entries;
  for (Metric& metric : metrics_) {
    const std::optional<double> value = metric.sampler->sample(now);
    // Clients can't parse NaN or Inf, which a tiny capacity_cores can produce.
    if (!value.has_value() || !std::isfinite(*value)) {
      continue;
    }
    setMetric(metric.target, *value, report);
    text_entries.push_back(absl::StrCat(metric.name, "=", *value));
  }
  // The request rate alone can't weight an endpoint, so it is only reported with a utilization
  // metric.
  if (text_entries.empty()) {
    return "";
  }
  // Weighted round robin needs a positive request rate to use the report. A zero rate is left
  // out, as an absent field already reads as zero.
  if (rps_ > 0) {
    report.set_rps_fractional(rps_);
    text_entries.push_back(absl::StrCat(Orca::kRpsFractionalField, "=", rps_));
  }

  switch (format_) {
  case ProtoConfig::TEXT:
    return absl::StrCat(Orca::kHeaderFormatPrefixText, absl::StrJoin(text_entries, ","));
  case ProtoConfig::JSON:
#ifdef ENVOY_ENABLE_YAML
    return absl::StrCat(Orca::kHeaderFormatPrefixJson,
                        MessageUtil::getJsonStringFromMessageOrError(report));
#else
    PANIC("not reached");
#endif
  case ProtoConfig::BINARY: {
    // Deterministic, so that an unchanged report encodes the same and is not republished.
    const std::string serialized = MessageUtil::serializeDeterministically(report);
    return Base64::encode(serialized.data(), serialized.size());
  }
  default:
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
}

void LoadReporter::publish(std::string report) {
  published_report_ = report;
  // set() rather than runOnAllThreads(), so that workers registered later also start with the
  // latest report. The report is immutable and shared by all threads.
  auto shared = std::make_shared<ThreadLocalReport>(std::move(report));
  tls_->set([shared = std::move(shared)](Event::Dispatcher&) { return shared; });
}

void LoadReporter::onRefreshTimer() {
  if (tls_->isShutdown()) {
    return;
  }
  std::string report = sampleAndEncode();
  if (report != published_report_) {
    publish(std::move(report));
  }
  refresh_timer_->enableTimer(refresh_interval_);
}

namespace {

bool hasReport(const Http::HeaderMap& headers) {
  return !headers.get(textHeader()).empty() || !headers.get(binaryHeader()).empty();
}

void removeReport(Http::HeaderMap& headers) {
  headers.remove(textHeader());
  headers.remove(binaryHeader());
}

} // namespace

Http::FilterHeadersStatus OrcaReporterFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  config_->reporter().onRequest();
  return Http::FilterHeadersStatus::Continue;
}

// The report is deliberately written to both the headers and the trailers. Envoy clients use the
// first report they see, which is the one in the headers, while gRPC clients only read the
// trailers. Writing both avoids guessing where the client looks, and the headers still carry a
// report if the response ends without trailers.
Http::FilterHeadersStatus OrcaReporterFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                            bool) {
  writeReport(headers);
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterTrailersStatus OrcaReporterFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  writeReport(trailers);
  return Http::FilterTrailersStatus::Continue;
}

void OrcaReporterFilter::writeReport(Http::HeaderMap& headers) const {
  const LoadReporter& reporter = config_->reporter();
  const absl::string_view report = reporter.encodedReport();
  // An existing report is kept when there is nothing to replace it with.
  if (report.empty()) {
    return;
  }
  if (hasReport(headers)) {
    if (!config_->overwriteExistingReport()) {
      return;
    }
    removeReport(headers);
  }
  headers.addCopy(reporter.headerName(), report);
}

} // namespace OrcaReporter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
