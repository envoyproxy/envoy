#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include <algorithm>

#include "source/common/tracing/null_span_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

using ::opentelemetry::proto::metrics::v1::AggregationTemporality;
using ::opentelemetry::proto::metrics::v1::HistogramDataPoint;
using ::opentelemetry::proto::metrics::v1::NumberDataPoint;

using MetricsExportRequest =
    opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest;
using Metric = opentelemetry::proto::metrics::v1::Metric;
using NumberDataPoint = opentelemetry::proto::metrics::v1::NumberDataPoint;
using HistogramDataPoint = opentelemetry::proto::metrics::v1::HistogramDataPoint;
using KeyValue = opentelemetry::proto::common::v1::KeyValue;
using Resource = opentelemetry::proto::resource::v1::Resource;

ResourceRegistry::ResourceRegistry(const Protobuf::RepeatedPtrField<KeyValue>& sink_attributes) {
  *resources_.emplace_back().mutable_attributes() = sink_attributes;
}

uint32_t ResourceRegistry::findOrAdd(const ConversionAction::ResourceConversion& conversion,
                                     const TagValues& tag_values) {
  ASSERT(tag_values.size() == conversion.tags.size());
  std::pair<const ConversionAction::ResourceConversion*, TagValues> key(&conversion, tag_values);
  const auto it = indexes_by_tag_values_.find(key);
  if (it != indexes_by_tag_values_.end()) {
    return it->second;
  }

  // The resource has the static attributes of the conversion and the attributes that its tags set.
  Attributes attributes(conversion.static_attributes.begin(), conversion.static_attributes.end());
  for (size_t i = 0; i < tag_values.size(); ++i) {
    ASSERT(!tag_values[i].empty());
    for (const std::string& attribute_key : conversion.tags[i].attribute_keys) {
      attributes.emplace_back(attribute_key, tag_values[i]);
    }
    // The key outlives the stat that the value belongs to.
    key.second[i] = tag_values_storage_.emplace_back(tag_values[i]);
  }
  std::sort(attributes.begin(), attributes.end());
  const uint32_t index = findOrAdd(std::move(attributes));
  indexes_by_tag_values_.emplace(std::move(key), index);
  return index;
}

uint32_t ResourceRegistry::findOrAdd(Attributes&& sorted_attributes) {
  if (indexes_by_attributes_.empty()) {
    // A converted resource with the same attributes as the sink resource is the sink resource.
    Attributes sink_attributes;
    sink_attributes.reserve(resources_[SinkResourceIndex].attributes_size());
    for (const KeyValue& attribute : resources_[SinkResourceIndex].attributes()) {
      sink_attributes.emplace_back(attribute.key(), attribute.value().string_value());
    }
    std::sort(sink_attributes.begin(), sink_attributes.end());
    indexes_by_attributes_.emplace(std::move(sink_attributes), SinkResourceIndex);
  }

  const auto [it, inserted] = indexes_by_attributes_.try_emplace(
      std::move(sorted_attributes), static_cast<uint32_t>(resources_.size()));
  if (inserted) {
    Resource& resource = resources_.emplace_back();
    for (const auto& [key, value] : it->first) {
      KeyValue* attribute = resource.add_attributes();
      attribute->set_key(key);
      attribute->mutable_value()->set_string_value(value);
    }
  }
  return it->second;
}

std::vector<MetricAggregator::AggregationResult> MetricAggregator::releaseResults() {
  return std::move(results_);
}

MetricAggregator::AggregationResult& MetricAggregator::resultOf(uint32_t resource_index) {
  if (resource_index >= results_.size()) {
    results_.resize(resource_index + 1);
  }
  return results_[resource_index];
}

void MetricAggregator::addGauge(std::string&& metric_name, uint64_t value,
                                SortedAttributesVector&& attributes, uint32_t resource_index) {
  auto& gauge_data = resultOf(resource_index).gauge_data_;
  MetricKey key{std::move(metric_name), std::move(attributes)};
  auto it = gauge_data.find(key);
  if (it != gauge_data.end()) {
    it->second += value;
  } else {
    gauge_data.emplace(std::move(key), value);
  }
}

void MetricAggregator::addCounter(std::string&& metric_name, uint64_t value,
                                  SortedAttributesVector&& attributes, uint32_t resource_index) {
  if (value == 0 && counter_temporality_ == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) {
    return;
  }

  auto& counter_data = resultOf(resource_index).counter_data_;
  MetricKey key{std::move(metric_name), std::move(attributes)};
  auto it = counter_data.find(key);

  if (it != counter_data.end()) {
    it->second += value;
  } else {
    counter_data.emplace(std::move(key), value);
  }
}

void MetricAggregator::addHistogram(std::string&& metric_name,
                                    const Envoy::Stats::HistogramStatistics& stats,
                                    SortedAttributesVector&& attributes, uint32_t resource_index) {
  if (stats.sampleCount() == 0 &&
      histogram_temporality_ == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) {
    return;
  }

  auto& histogram_data = resultOf(resource_index).histogram_data_;
  MetricKey key{std::move(metric_name), std::move(attributes)};
  auto it = histogram_data.find(key);
  if (it != histogram_data.end()) {
    std::vector<uint64_t> new_bucket_counts = stats.computeDisjointBuckets();
    auto& existing_point = it->second;
    if (existing_point.explicit_bounds_ == stats.supportedBuckets() &&
        existing_point.bucket_counts_.size() == new_bucket_counts.size() + 1) {
      existing_point.count_ += stats.sampleCount();
      existing_point.sum_ += stats.sampleSum();
      for (size_t i = 0; i < new_bucket_counts.size(); ++i) {
        existing_point.bucket_counts_[i] += new_bucket_counts[i];
      }
      existing_point.bucket_counts_.back() += stats.outOfBoundCount();
    } else {
      ENVOY_LOG(error, "Histogram bounds mismatch for metric {} aggregated from stat", key.name());
    }
  } else {
    CustomHistogram custom_hist;
    custom_hist.count_ = stats.sampleCount();
    custom_hist.sum_ = stats.sampleSum();
    custom_hist.bucket_counts_ = stats.computeDisjointBuckets();
    custom_hist.bucket_counts_.push_back(stats.outOfBoundCount());
    custom_hist.explicit_bounds_ = stats.supportedBuckets();
    histogram_data.emplace(std::move(key), std::move(custom_hist));
  }
}

RequestStreamer::RequestStreamer(uint32_t max_dp, const ResourceRegistry& resources,
                                 AggregationTemporality counter_temporality,
                                 AggregationTemporality histogram_temporality,
                                 absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback,
                                 int64_t snapshot_time_ns, int64_t delta_start_time_ns,
                                 int64_t cumulative_start_time_ns, bool enable_metric_aggregation)
    : max_dp_(max_dp), enable_metric_aggregation_(enable_metric_aggregation), resources_(resources),
      counter_temporality_(counter_temporality), histogram_temporality_(histogram_temporality),
      send_callback_(std::move(send_callback)), snapshot_time_ns_(snapshot_time_ns),
      delta_start_time_ns_(delta_start_time_ns),
      cumulative_start_time_ns_(cumulative_start_time_ns) {}

void RequestStreamer::sendIfFullAndPrepareRequest() {
  if (current_request_ != nullptr && (max_dp_ == 0 || dp_num_ < max_dp_)) {
    return;
  }
  send();
  initNewRequest();
}

void RequestStreamer::initNewRequest() {
  if (current_request_ != nullptr) {
    return;
  }
  current_request_ = std::make_unique<MetricsExportRequest>();
  // The ResourceMetrics of each resource is added along with the first data point of the resource.
  resource_scope_metrics_.clear();
  name_to_metric_.clear();
  dp_num_ = 0;
}

::opentelemetry::proto::metrics::v1::ScopeMetrics&
RequestStreamer::scopeMetricsOf(uint32_t resource_index) {
  if (resource_index >= resource_scope_metrics_.size()) {
    resource_scope_metrics_.resize(resource_index + 1, nullptr);
  }
  auto*& scope_metrics = resource_scope_metrics_[resource_index];
  if (scope_metrics == nullptr) {
    auto* resource_metrics = current_request_->add_resource_metrics();
    const Resource& resource = resources_.get(resource_index);
    if (!resource.attributes().empty()) {
      *resource_metrics->mutable_resource() = resource;
    }
    scope_metrics = resource_metrics->add_scope_metrics();
  }
  return *scope_metrics;
}

::opentelemetry::proto::metrics::v1::Metric*
RequestStreamer::findOrCreateMetric(std::string&& name, uint32_t resource_index) {
  if (enable_metric_aggregation_) {
    auto it = name_to_metric_.find(std::make_pair(resource_index, absl::string_view(name)));
    if (it != name_to_metric_.end()) {
      return it->second;
    }
  }

  auto* m = scopeMetricsOf(resource_index).add_metrics();
  m->set_name(std::move(name));

  if (enable_metric_aggregation_) {
    name_to_metric_.emplace(std::make_pair(resource_index, absl::string_view(m->name())), m);
  }
  return m;
}

template <class PointType>
void RequestStreamer::setCommonFields(PointType* point,
                                      MetricAggregator::SortedAttributesVector&& attributes,
                                      AggregationTemporality temp) const {
  point->set_time_unix_nano(snapshot_time_ns_);
  if (temp == AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE) {
    point->set_start_time_unix_nano(cumulative_start_time_ns_);
  } else if (temp == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) {
    point->set_start_time_unix_nano(delta_start_time_ns_);
  }

  auto attrs = std::move(attributes).release();
  for (auto& [k, v] : attrs) {
    auto* attr = point->add_attributes();
    attr->set_key(std::move(k));
    attr->mutable_value()->set_string_value(std::move(v));
  }
}

void RequestStreamer::addGauge(std::string&& name, uint64_t value,
                               MetricAggregator::SortedAttributesVector&& attributes,
                               uint32_t resource_index) {
  sendIfFullAndPrepareRequest();
  auto* metric = findOrCreateMetric(std::move(name), resource_index);
  auto* point = metric->mutable_gauge()->add_data_points();
  point->set_as_int(value);
  setCommonFields(point, std::move(attributes),
                  AggregationTemporality::AGGREGATION_TEMPORALITY_UNSPECIFIED);
  dp_num_++;
}

void RequestStreamer::addCounter(std::string&& name, uint64_t value,
                                 MetricAggregator::SortedAttributesVector&& attributes,
                                 uint32_t resource_index) {
  sendIfFullAndPrepareRequest();
  auto temp = counter_temporality_;
  if (value == 0 && temp == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) {
    return;
  }
  auto* metric = findOrCreateMetric(std::move(name), resource_index);
  if (metric->mutable_sum()->data_points_size() == 0) {
    metric->mutable_sum()->set_is_monotonic(true);
    metric->mutable_sum()->set_aggregation_temporality(temp);
  }
  auto* point = metric->mutable_sum()->add_data_points();
  point->set_as_int(value);
  setCommonFields(point, std::move(attributes), temp);
  dp_num_++;
}

void RequestStreamer::addHistogram(std::string&& name, MetricAggregator::CustomHistogram hist,
                                   MetricAggregator::SortedAttributesVector&& attributes,
                                   uint32_t resource_index) {
  sendIfFullAndPrepareRequest();
  auto temp = histogram_temporality_;
  if (hist.count_ == 0 && temp == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) {
    return;
  }
  auto* metric = findOrCreateMetric(std::move(name), resource_index);
  if (metric->mutable_histogram()->data_points_size() == 0) {
    metric->mutable_histogram()->set_aggregation_temporality(temp);
  }
  auto* point = metric->mutable_histogram()->add_data_points();
  point->set_count(hist.count_);
  point->set_sum(hist.sum_);
  // TODO(ohadvano): support min/max/variance for histograms as per OTLP spec.
  for (double bound : hist.explicit_bounds_) {
    point->add_explicit_bounds(bound);
  }
  for (uint64_t count : hist.bucket_counts_) {
    point->add_bucket_counts(count);
  }
  setCommonFields(point, std::move(attributes), temp);
  dp_num_++;
}

void RequestStreamer::addHistogram(std::string&& name,
                                   const Envoy::Stats::HistogramStatistics& stats,
                                   MetricAggregator::SortedAttributesVector&& attributes,
                                   uint32_t resource_index) {
  MetricAggregator::CustomHistogram custom_hist;
  custom_hist.count_ = stats.sampleCount();
  custom_hist.sum_ = stats.sampleSum();
  custom_hist.bucket_counts_ = stats.computeDisjointBuckets();
  custom_hist.bucket_counts_.push_back(stats.outOfBoundCount());
  custom_hist.explicit_bounds_ = stats.supportedBuckets();
  addHistogram(std::move(name), std::move(custom_hist), std::move(attributes), resource_index);
}

void RequestStreamer::addAggregationResults(
    std::vector<MetricAggregator::AggregationResult>&& results) {
  // Adding the data points of one resource after another keeps the number of ResourceMetrics in
  // each request low when the data points are split into multiple requests.
  for (uint32_t resource_index = 0; resource_index < results.size(); ++resource_index) {
    MetricAggregator::AggregationResult& metrics = results[resource_index];
    // Extracting a node doesn't invalidate the iterators to the other nodes, so each map is
    // iterated once instead of searched for its first node after each extraction.
    for (auto it = metrics.gauge_data_.begin(); it != metrics.gauge_data_.end();) {
      auto node = metrics.gauge_data_.extract(it++);
      auto& key = node.key();
      addGauge(key.releaseName(), node.mapped(), key.releaseAttributes(), resource_index);
    }
    for (auto it = metrics.counter_data_.begin(); it != metrics.counter_data_.end();) {
      auto node = metrics.counter_data_.extract(it++);
      auto& key = node.key();
      addCounter(key.releaseName(), node.mapped(), key.releaseAttributes(), resource_index);
    }
    for (auto it = metrics.histogram_data_.begin(); it != metrics.histogram_data_.end();) {
      auto node = metrics.histogram_data_.extract(it++);
      auto& key = node.key();
      addHistogram(key.releaseName(), std::move(node.mapped()), key.releaseAttributes(),
                   resource_index);
    }
  }
}

void RequestStreamer::send() {
  if (current_request_ != nullptr && dp_num_ > 0) {
    send_callback_(std::move(current_request_));
    current_request_ = nullptr;
  }
}

Protobuf::RepeatedPtrField<KeyValue>
generateResourceAttributes(const Tracers::OpenTelemetry::Resource& resource) {
  Protobuf::RepeatedPtrField<KeyValue> result;

  for (const auto& [key, value] : resource.attributes_) {
    auto* attr = result.Add();
    attr->set_key(key);
    attr->mutable_value()->set_string_value(value);
  }
  return result;
}

Matcher::MatchTreePtr<Stats::StatMatchingData>
createMatcher(const xds::type::matcher::v3::Matcher& matcher_config,
              Server::Configuration::ServerFactoryContext& server_factory_context,
              absl::Status& creation_status) {
  ActionValidationVisitor validation_visitor;
  ActionContext action_context;
  Matcher::MatchTreeFactory<Stats::StatMatchingData, ActionContext> factory{
      action_context, server_factory_context, validation_visitor};
  // The actions are created along with the matcher, so their errors are known once it is created.
  Matcher::MatchTreePtr<Stats::StatMatchingData> matcher = factory.create(matcher_config)();
  creation_status = action_context.creation_status;
  return matcher;
}

absl::StatusOr<OtlpOptionsSharedPtr>
OtlpOptions::create(const SinkConfig& sink_config, const Tracers::OpenTelemetry::Resource& resource,
                    Server::Configuration::ServerFactoryContext& server) {
  absl::Status creation_status = absl::OkStatus();
  OtlpOptionsSharedPtr options(new OtlpOptions(sink_config, resource, server, creation_status));
  RETURN_IF_NOT_OK_REF(creation_status);
  return options;
}

OtlpOptions::OtlpOptions(const SinkConfig& sink_config,
                         const Tracers::OpenTelemetry::Resource& resource,
                         Server::Configuration::ServerFactoryContext& server,
                         absl::Status& creation_status)
    : report_counters_as_deltas_(sink_config.report_counters_as_deltas()),
      report_histograms_as_deltas_(sink_config.report_histograms_as_deltas()),
      emit_tags_as_attributes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, emit_tags_as_attributes, true)),
      use_tag_extracted_name_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, use_tag_extracted_name, true)),
      stat_prefix_(!sink_config.prefix().empty() ? sink_config.prefix() + "." : ""),
      enable_metric_aggregation_(sink_config.has_custom_metric_conversions()),
      resource_attributes_(generateResourceAttributes(resource)),
      matcher_(createMatcher(sink_config.custom_metric_conversions(), server, creation_status)),
      max_data_points_per_request_(sink_config.max_data_points_per_request()) {}

OpenTelemetryGrpcMetricsExporterImpl::OpenTelemetryGrpcMetricsExporterImpl(
    const OtlpOptionsSharedPtr config, Grpc::RawAsyncClientSharedPtr raw_async_client)
    : config_(config), client_(raw_async_client),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "opentelemetry.proto.collector.metrics.v1.MetricsService."
          "Export")) {}

void OpenTelemetryGrpcMetricsExporterImpl::send(MetricsExportRequestPtr&& export_request) {
  ENVOY_LOG(debug, "sending a OTLP metric request: {}", export_request->DebugString());
  client_->send(service_method_, *export_request, *this, Tracing::NullSpan::instance(),
                Http::AsyncClient::RequestOptions());
}

void OpenTelemetryGrpcMetricsExporterImpl::onSuccess(
    Grpc::ResponsePtr<MetricsExportResponse>&& export_response, Tracing::Span&) {
  if (export_response->has_partial_success()) {
    ENVOY_LOG(debug,
              "export response with partial success; {} rejected, collector "
              "message: {}",
              export_response->partial_success().rejected_data_points(),
              export_response->partial_success().error_message());
  }
}

void OpenTelemetryGrpcMetricsExporterImpl::onFailure(Grpc::Status::GrpcStatus response_status,
                                                     const std::string& response_message,
                                                     Tracing::Span&) {
  ENVOY_LOG(debug, "export failure; status: {}, message: {}", response_status, response_message);
}

template <class StatType>
OtlpMetricsFlusherImpl::MetricConfig
OtlpMetricsFlusherImpl::getMetricConfig(const StatType& stat) const {
  Stats::StatMatchingDataImpl<StatType> data(stat);
  const ::Envoy::Matcher::ActionMatchResult result =
      Envoy::Matcher::evaluateMatch<Stats::StatMatchingData>(*config_->matcher(), data);
  ASSERT(result.isComplete());
  if (result.isMatch()) {
    if (dynamic_cast<const DropAction*>(result.action().get())) {
      return {true, nullptr};
    }

    if (const auto* match_action = dynamic_cast<const ConversionAction*>(result.action().get())) {
      return {false, match_action};
    }

    ENVOY_LOG(error, "Unknown action type for custom metric conversion: {}",
              result.action()->typeUrl());
  }

  // By default, this stat will be converted to the metric without any
  // customization.
  return {false, nullptr};
}

template <class StatType>
std::string OtlpMetricsFlusherImpl::getMetricName(const StatType& stat,
                                                  const ConversionAction* conversion_action) const {
  // A conversion without a metric name keeps the default name of the stat, since an empty name is
  // not a valid OTLP metric name.
  if (conversion_action != nullptr && !conversion_action->config()->metric_name().empty()) {
    return conversion_action->config()->metric_name();
  }
  return absl::StrCat(config_->statPrefix(),
                      config_->useTagExtractedName() ? stat.tagExtractedName() : stat.name());
}

namespace {

// Adds the tags of a stat to `attributes`, if tags are emitted as attributes, except for the tags
// that set the attributes of the resource of `conversion`. Returns the index of the resource that
// the stat is reported under, which is added to `resources` if it is new. If the stat lacks one of
// the tags of `conversion`, or has it with an empty value, the stat is reported as if it had no
// resource conversion: under the sink resource, with all of its tags.
uint32_t addTagAttributes(const Stats::TagVector& tags,
                          const ConversionAction::ResourceConversion& conversion,
                          bool emit_tags_as_attributes,
                          MetricAggregator::AttributesVector& attributes,
                          ResourceRegistry& resources) {
  ResourceRegistry::TagValues tag_values(conversion.tags.size());
  size_t num_tag_values = 0;
  for (const Stats::Tag& tag : tags) {
    const auto it = conversion.tag_indexes.find(tag.name_);
    if (it == conversion.tag_indexes.end()) {
      if (emit_tags_as_attributes) {
        attributes.emplace_back(tag.name_, tag.value_);
      }
    } else if (!tag.value_.empty() && tag_values[it->second].empty()) {
      tag_values[it->second] = tag.value_;
      ++num_tag_values;
    }
  }
  if (num_tag_values == tag_values.size()) {
    return resources.findOrAdd(conversion, tag_values);
  }

  if (emit_tags_as_attributes) {
    for (const Stats::Tag& tag : tags) {
      if (conversion.tag_indexes.contains(tag.name_)) {
        attributes.emplace_back(tag.name_, tag.value_);
      }
    }
  }
  return ResourceRegistry::SinkResourceIndex;
}

} // namespace

template <class StatType>
OtlpMetricsFlusherImpl::DataPointAttributes
OtlpMetricsFlusherImpl::getCombinedAttributes(const StatType& stat,
                                              const ConversionAction* conversion_action,
                                              ResourceRegistry& resources) const {
  MetricAggregator::AttributesVector attrs;
  uint32_t resource_index = ResourceRegistry::SinkResourceIndex;
  const ConversionAction::ResourceConversion* resource_conversion =
      conversion_action != nullptr ? conversion_action->resourceConversion() : nullptr;
  if (resource_conversion != nullptr) {
    resource_index = addTagAttributes(stat.tags(), *resource_conversion,
                                      config_->emitTagsAsAttributes(), attrs, resources);
  } else if (config_->emitTagsAsAttributes()) {
    for (const auto& tag : stat.tags()) {
      attrs.emplace_back(tag.name_, tag.value_);
    }
  }
  if (conversion_action != nullptr) {
    for (const auto& attr : conversion_action->config()->static_metric_labels()) {
      attrs.emplace_back(attr.key(), attr.value().string_value());
    }
  }
  // Sort attributes to ensure a deterministic order, which is critical since these
  // attributes are used as part of the lookup key for metric aggregation.
  std::sort(attrs.begin(), attrs.end());

  return {MetricAggregator::SortedAttributesVector(std::move(attrs)), resource_index};
}

template <typename SinkType>
void OtlpMetricsFlusherImpl::sinkMetrics(Stats::MetricSnapshot& snapshot,
                                         ResourceRegistry& resources, SinkType& sink) const {
  // Process Gauges
  for (const auto& gauge : snapshot.gauges()) {
    const auto& g = gauge.get();
    if (predicate_(g)) {
      auto metric_config = getMetricConfig(g);
      if (metric_config.drop_stat) {
        continue;
      }
      auto [attributes, resource_index] =
          getCombinedAttributes(g, metric_config.conversion_action, resources);
      sink.addGauge(getMetricName(g, metric_config.conversion_action), g.value(),
                    std::move(attributes), resource_index);
    }
  }
  for (const auto& gauge : snapshot.hostGauges()) {
    auto metric_config = getMetricConfig(gauge);
    if (metric_config.drop_stat) {
      continue;
    }
    auto [attributes, resource_index] =
        getCombinedAttributes(gauge, metric_config.conversion_action, resources);
    sink.addGauge(getMetricName(gauge, metric_config.conversion_action), gauge.value(),
                  std::move(attributes), resource_index);
  }

  // Process Counters
  const bool report_counters_as_deltas = config_->reportCountersAsDeltas();
  for (const auto& counter : snapshot.counters()) {
    const auto& c = counter.counter_.get();
    if (predicate_(c)) {
      auto metric_config = getMetricConfig(c);
      if (metric_config.drop_stat) {
        continue;
      }
      const uint64_t counter_value = report_counters_as_deltas ? counter.delta_ : c.value();
      auto [attributes, resource_index] =
          getCombinedAttributes(c, metric_config.conversion_action, resources);
      sink.addCounter(getMetricName(c, metric_config.conversion_action), counter_value,
                      std::move(attributes), resource_index);
    }
  }
  for (const auto& counter : snapshot.hostCounters()) {
    auto metric_config = getMetricConfig(counter);
    if (metric_config.drop_stat) {
      continue;
    }
    const uint64_t counter_value = report_counters_as_deltas ? counter.delta() : counter.value();
    auto [attributes, resource_index] =
        getCombinedAttributes(counter, metric_config.conversion_action, resources);
    sink.addCounter(getMetricName(counter, metric_config.conversion_action), counter_value,
                    std::move(attributes), resource_index);
  }

  // Process Histograms
  const bool report_histograms_as_deltas = config_->reportHistogramsAsDeltas();
  for (const auto& histogram : snapshot.histograms()) {
    const auto& h = histogram.get();
    if (predicate_(h)) {
      auto metric_config = getMetricConfig(h);
      if (metric_config.drop_stat) {
        continue;
      }
      const Stats::HistogramStatistics& histogram_stats =
          report_histograms_as_deltas ? h.intervalStatistics() : h.cumulativeStatistics();
      auto [attributes, resource_index] =
          getCombinedAttributes(h, metric_config.conversion_action, resources);
      sink.addHistogram(getMetricName(h, metric_config.conversion_action), histogram_stats,
                        std::move(attributes), resource_index);
    }
  }
}

void OtlpMetricsFlusherImpl::flush(
    Stats::MetricSnapshot& snapshot, int64_t delta_start_time_ns, int64_t cumulative_start_time_ns,
    absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback) const {

  const int64_t snapshot_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    snapshot.snapshotTime().time_since_epoch())
                                    .count();
  // The resources that the resource conversions resolve to are found anew in each flush.
  ResourceRegistry resources(config_->resource_attributes());
  RequestStreamer streamer(config_->maxDataPointsPerRequest(), resources, counter_temporality_,
                           histogram_temporality_, std::move(send_callback), snapshot_time,
                           delta_start_time_ns, cumulative_start_time_ns,
                           config_->enableMetricAggregation());

  if (!config_->enableMetricAggregation()) {
    sinkMetrics(snapshot, resources, streamer);
    streamer.send();
    return;
  }

  MetricAggregator aggregator(counter_temporality_, histogram_temporality_);
  sinkMetrics(snapshot, resources, aggregator);
  streamer.addAggregationResults(aggregator.releaseResults());
  streamer.send();
}

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
