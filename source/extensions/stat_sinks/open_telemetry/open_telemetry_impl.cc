#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/stat_sinks/open_telemetry/stat_match_action.h"

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

MetricAggregator::AggregationResult MetricAggregator::releaseResult() {
  resource_ids_.clear();
  return {std::move(gauge_data_), std::move(counter_data_), std::move(histogram_data_),
          std::move(resources_)};
}

uint32_t MetricAggregator::resourceId(std::optional<SortedAttributesVector>&& resource) {
  if (!resource.has_value()) {
    return kDefaultResourceId;
  }
  auto it = resource_ids_.find(*resource);
  if (it != resource_ids_.end()) {
    return it->second;
  }
  const uint32_t id = resources_.size() + 1;
  resources_.push_back(*resource);
  resource_ids_.emplace(std::move(*resource), id);
  return id;
}

void MetricAggregator::addGauge(std::string&& metric_name, uint64_t value,
                                SortedAttributesVector&& attributes,
                                std::optional<SortedAttributesVector>&& resource) {
  MetricKey key{std::move(metric_name), std::move(attributes), resourceId(std::move(resource))};
  auto it = gauge_data_.find(key);
  if (it != gauge_data_.end()) {
    it->second += value;
  } else {
    gauge_data_.emplace(std::move(key), value);
  }
}

void MetricAggregator::addCounter(std::string&& metric_name, uint64_t value,
                                  SortedAttributesVector&& attributes,
                                  std::optional<SortedAttributesVector>&& resource) {
  if (value == 0 && counter_temporality_ == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) {
    return;
  }

  MetricKey key{std::move(metric_name), std::move(attributes), resourceId(std::move(resource))};
  auto it = counter_data_.find(key);

  if (it != counter_data_.end()) {
    it->second += value;
  } else {
    counter_data_.emplace(std::move(key), value);
  }
}

void MetricAggregator::addHistogram(std::string&& metric_name,
                                    const Envoy::Stats::HistogramStatistics& stats,
                                    SortedAttributesVector&& attributes,
                                    std::optional<SortedAttributesVector>&& resource) {
  if (stats.sampleCount() == 0 &&
      histogram_temporality_ == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) {
    return;
  }

  MetricKey key{std::move(metric_name), std::move(attributes), resourceId(std::move(resource))};
  auto it = histogram_data_.find(key);
  if (it != histogram_data_.end()) {
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
    histogram_data_.emplace(std::move(key), std::move(custom_hist));
  }
}

RequestStreamer::RequestStreamer(
    uint32_t max_dp,
    const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>&
        resource_attributes,
    AggregationTemporality counter_temporality, AggregationTemporality histogram_temporality,
    absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback, int64_t snapshot_time_ns,
    int64_t delta_start_time_ns, int64_t cumulative_start_time_ns, bool enable_metric_aggregation)
    : max_dp_(max_dp), enable_metric_aggregation_(enable_metric_aggregation),
      resource_attributes_(resource_attributes), counter_temporality_(counter_temporality),
      histogram_temporality_(histogram_temporality), send_callback_(std::move(send_callback)),
      snapshot_time_ns_(snapshot_time_ns), delta_start_time_ns_(delta_start_time_ns),
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
  // The ResourceMetrics of each resource is added along with the resource's first data point.
  default_resource_slot_ = ResourceSlot{};
  resource_slots_.clear();
  dp_num_ = 0;
}

RequestStreamer::ResourceSlot&
RequestStreamer::slotFor(const MetricAggregator::SortedAttributesVector* resource) {
  ASSERT(current_request_ != nullptr);
  ResourceSlot* slot = &default_resource_slot_;
  if (resource != nullptr) {
    auto it = resource_slots_.find(*resource);
    if (it == resource_slots_.end()) {
      it = resource_slots_.emplace(*resource, ResourceSlot{}).first;
    }
    slot = &it->second;
  }
  if (slot->scope_metrics == nullptr) {
    auto* rm = current_request_->add_resource_metrics();
    if (resource == nullptr) {
      for (const auto& attr : resource_attributes_) {
        *rm->mutable_resource()->add_attributes() = attr;
      }
    } else {
      for (const auto& [key, value] : resource->attributes()) {
        auto* attr = rm->mutable_resource()->add_attributes();
        attr->set_key(key);
        attr->mutable_value()->set_string_value(value);
      }
    }
    slot->scope_metrics = rm->add_scope_metrics();
  }
  return *slot;
}

::opentelemetry::proto::metrics::v1::Metric*
RequestStreamer::findOrCreateMetric(ResourceSlot& slot, std::string&& name) {
  if (enable_metric_aggregation_) {
    auto it = slot.name_to_metric.find(name);
    if (it != slot.name_to_metric.end()) {
      return it->second;
    }
  }

  auto* m = slot.scope_metrics->add_metrics();
  m->set_name(std::move(name));

  if (enable_metric_aggregation_) {
    slot.name_to_metric.emplace(m->name(), m);
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

void RequestStreamer::addGaugeTo(const MetricAggregator::SortedAttributesVector* resource,
                                 std::string&& name, uint64_t value,
                                 MetricAggregator::SortedAttributesVector&& attributes) {
  sendIfFullAndPrepareRequest();
  auto* metric = findOrCreateMetric(slotFor(resource), std::move(name));
  auto* point = metric->mutable_gauge()->add_data_points();
  point->set_as_int(value);
  setCommonFields(point, std::move(attributes),
                  AggregationTemporality::AGGREGATION_TEMPORALITY_UNSPECIFIED);
  dp_num_++;
}

void RequestStreamer::addCounterTo(const MetricAggregator::SortedAttributesVector* resource,
                                   std::string&& name, uint64_t value,
                                   MetricAggregator::SortedAttributesVector&& attributes) {
  auto temp = counter_temporality_;
  if (value == 0 && temp == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) {
    return;
  }
  sendIfFullAndPrepareRequest();
  auto* metric = findOrCreateMetric(slotFor(resource), std::move(name));
  if (metric->mutable_sum()->data_points_size() == 0) {
    metric->mutable_sum()->set_is_monotonic(true);
    metric->mutable_sum()->set_aggregation_temporality(temp);
  }
  auto* point = metric->mutable_sum()->add_data_points();
  point->set_as_int(value);
  setCommonFields(point, std::move(attributes), temp);
  dp_num_++;
}

void RequestStreamer::addHistogramTo(const MetricAggregator::SortedAttributesVector* resource,
                                     std::string&& name, MetricAggregator::CustomHistogram hist,
                                     MetricAggregator::SortedAttributesVector&& attributes) {
  auto temp = histogram_temporality_;
  if (hist.count_ == 0 && temp == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) {
    return;
  }
  sendIfFullAndPrepareRequest();
  auto* metric = findOrCreateMetric(slotFor(resource), std::move(name));
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

void RequestStreamer::addGauge(std::string&& name, uint64_t value,
                               MetricAggregator::SortedAttributesVector&& attributes,
                               std::optional<MetricAggregator::SortedAttributesVector>&& resource) {
  addGaugeTo(resource.has_value() ? &*resource : nullptr, std::move(name), value,
             std::move(attributes));
}

void RequestStreamer::addCounter(
    std::string&& name, uint64_t value, MetricAggregator::SortedAttributesVector&& attributes,
    std::optional<MetricAggregator::SortedAttributesVector>&& resource) {
  addCounterTo(resource.has_value() ? &*resource : nullptr, std::move(name), value,
               std::move(attributes));
}

void RequestStreamer::addHistogram(
    std::string&& name, MetricAggregator::CustomHistogram hist,
    MetricAggregator::SortedAttributesVector&& attributes,
    std::optional<MetricAggregator::SortedAttributesVector>&& resource) {
  addHistogramTo(resource.has_value() ? &*resource : nullptr, std::move(name), std::move(hist),
                 std::move(attributes));
}

void RequestStreamer::addHistogram(
    std::string&& name, const Envoy::Stats::HistogramStatistics& stats,
    MetricAggregator::SortedAttributesVector&& attributes,
    std::optional<MetricAggregator::SortedAttributesVector>&& resource) {
  MetricAggregator::CustomHistogram custom_hist;
  custom_hist.count_ = stats.sampleCount();
  custom_hist.sum_ = stats.sampleSum();
  custom_hist.bucket_counts_ = stats.computeDisjointBuckets();
  custom_hist.bucket_counts_.push_back(stats.outOfBoundCount());
  custom_hist.explicit_bounds_ = stats.supportedBuckets();
  addHistogram(std::move(name), std::move(custom_hist), std::move(attributes), std::move(resource));
}

void RequestStreamer::addAggregationResult(MetricAggregator::AggregationResult&& metrics) {
  // Group the data points by resource, so that the points of a resource are contiguous and each
  // request has as few ResourceMetrics as possible.
  using GaugeNode = decltype(metrics.gauge_data_)::node_type;
  using CounterNode = decltype(metrics.counter_data_)::node_type;
  using HistogramNode = decltype(metrics.histogram_data_)::node_type;
  const size_t num_resources = metrics.resources_.size() + 1;
  std::vector<std::vector<GaugeNode>> gauges(num_resources);
  std::vector<std::vector<CounterNode>> counters(num_resources);
  std::vector<std::vector<HistogramNode>> histograms(num_resources);
  while (!metrics.gauge_data_.empty()) {
    auto node = metrics.gauge_data_.extract(metrics.gauge_data_.begin());
    gauges[node.key().resourceId()].push_back(std::move(node));
  }
  while (!metrics.counter_data_.empty()) {
    auto node = metrics.counter_data_.extract(metrics.counter_data_.begin());
    counters[node.key().resourceId()].push_back(std::move(node));
  }
  while (!metrics.histogram_data_.empty()) {
    auto node = metrics.histogram_data_.extract(metrics.histogram_data_.begin());
    histograms[node.key().resourceId()].push_back(std::move(node));
  }

  for (size_t id = 0; id < num_resources; ++id) {
    const MetricAggregator::SortedAttributesVector* resource =
        id == MetricAggregator::kDefaultResourceId ? nullptr : &metrics.resources_[id - 1];
    for (auto& node : gauges[id]) {
      auto& key = node.key();
      addGaugeTo(resource, key.releaseName(), node.mapped(), key.releaseAttributes());
    }
    for (auto& node : counters[id]) {
      auto& key = node.key();
      addCounterTo(resource, key.releaseName(), node.mapped(), key.releaseAttributes());
    }
    for (auto& node : histograms[id]) {
      auto& key = node.key();
      addHistogramTo(resource, key.releaseName(), std::move(node.mapped()),
                     key.releaseAttributes());
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
              const Protobuf::RepeatedPtrField<KeyValue>& resource_attributes,
              Server::Configuration::ServerFactoryContext& server_factory_context) {
  ActionValidationVisitor validation_visitor;
  ActionContext action_context{&resource_attributes};
  Matcher::MatchTreeFactory<Stats::StatMatchingData, ActionContext> factory{
      action_context, server_factory_context, validation_visitor};
  return factory.create(matcher_config)();
}

OtlpOptions::OtlpOptions(const SinkConfig& sink_config,
                         const Tracers::OpenTelemetry::Resource& resource,
                         Server::Configuration::ServerFactoryContext& server)
    : report_counters_as_deltas_(sink_config.report_counters_as_deltas()),
      report_histograms_as_deltas_(sink_config.report_histograms_as_deltas()),
      emit_tags_as_attributes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, emit_tags_as_attributes, true)),
      use_tag_extracted_name_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, use_tag_extracted_name, true)),
      stat_prefix_(!sink_config.prefix().empty() ? sink_config.prefix() + "." : ""),
      enable_metric_aggregation_(sink_config.has_custom_metric_conversions()),
      resource_attributes_(generateResourceAttributes(resource)),
      matcher_(
          createMatcher(sink_config.custom_metric_conversions(), resource_attributes_, server)),
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
  if (conversion_action != nullptr) {
    return conversion_action->config()->metric_name();
  }
  return absl::StrCat(config_->statPrefix(),
                      config_->useTagExtractedName() ? stat.tagExtractedName() : stat.name());
}

template <class StatType>
OtlpMetricsFlusherImpl::StatAttributes
OtlpMetricsFlusherImpl::getAttributes(const StatType& stat,
                                      const ConversionAction* conversion_action) const {
  const ConversionAction::ResourceConversion* resource_conversion =
      conversion_action != nullptr ? conversion_action->resourceConversion() : nullptr;

  MetricAggregator::AttributesVector attrs;
  std::optional<MetricAggregator::AttributesVector> resource;
  if (resource_conversion != nullptr) {
    resource.emplace(resource_conversion->base_attributes.begin(),
                     resource_conversion->base_attributes.end());
  }
  // Moves a tag listed by the resource conversion to the resource, overriding the base attribute
  // with the same key. Returns false if the tag is not listed.
  const auto move_to_resource = [&](const Stats::Tag& tag) {
    if (resource_conversion == nullptr) {
      return false;
    }
    const auto it = resource_conversion->tag_to_attribute_keys.find(tag.name_);
    if (it == resource_conversion->tag_to_attribute_keys.end()) {
      return false;
    }
    if (tag.value_.empty()) {
      return true;
    }
    for (const std::string& key : it->second) {
      auto existing = std::find_if(resource->begin(), resource->end(),
                                   [&key](const auto& attr) { return attr.first == key; });
      if (existing != resource->end()) {
        existing->second = tag.value_;
      } else {
        resource->emplace_back(key, tag.value_);
      }
    }
    return true;
  };

  const bool emit_tags_as_attributes = config_->emitTagsAsAttributes();
  if (emit_tags_as_attributes ||
      (resource_conversion != nullptr && !resource_conversion->tag_to_attribute_keys.empty())) {
    for (const auto& tag : stat.tags()) {
      if (!move_to_resource(tag) && emit_tags_as_attributes) {
        attrs.emplace_back(tag.name_, tag.value_);
      }
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

  StatAttributes result{MetricAggregator::SortedAttributesVector(std::move(attrs)), std::nullopt};
  if (resource.has_value()) {
    std::sort(resource->begin(), resource->end());
    result.resource.emplace(std::move(*resource));
  }
  return result;
}

template <typename SinkType>
void OtlpMetricsFlusherImpl::sinkMetrics(Stats::MetricSnapshot& snapshot, SinkType& sink) const {
  // Process Gauges
  for (const auto& gauge : snapshot.gauges()) {
    const auto& g = gauge.get();
    if (predicate_(g)) {
      auto metric_config = getMetricConfig(g);
      if (metric_config.drop_stat) {
        continue;
      }
      auto [attributes, resource] = getAttributes(g, metric_config.conversion_action);
      sink.addGauge(getMetricName(g, metric_config.conversion_action), g.value(),
                    std::move(attributes), std::move(resource));
    }
  }
  for (const auto& gauge : snapshot.hostGauges()) {
    auto metric_config = getMetricConfig(gauge);
    if (metric_config.drop_stat) {
      continue;
    }
    auto [attributes, resource] = getAttributes(gauge, metric_config.conversion_action);
    sink.addGauge(getMetricName(gauge, metric_config.conversion_action), gauge.value(),
                  std::move(attributes), std::move(resource));
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
      auto [attributes, resource] = getAttributes(c, metric_config.conversion_action);
      sink.addCounter(getMetricName(c, metric_config.conversion_action), counter_value,
                      std::move(attributes), std::move(resource));
    }
  }
  for (const auto& counter : snapshot.hostCounters()) {
    auto metric_config = getMetricConfig(counter);
    if (metric_config.drop_stat) {
      continue;
    }
    const uint64_t counter_value = report_counters_as_deltas ? counter.delta() : counter.value();
    auto [attributes, resource] = getAttributes(counter, metric_config.conversion_action);
    sink.addCounter(getMetricName(counter, metric_config.conversion_action), counter_value,
                    std::move(attributes), std::move(resource));
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
      auto [attributes, resource] = getAttributes(h, metric_config.conversion_action);
      sink.addHistogram(getMetricName(h, metric_config.conversion_action), histogram_stats,
                        std::move(attributes), std::move(resource));
    }
  }
}

void OtlpMetricsFlusherImpl::flush(
    Stats::MetricSnapshot& snapshot, int64_t delta_start_time_ns, int64_t cumulative_start_time_ns,
    absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback) const {

  const int64_t snapshot_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    snapshot.snapshotTime().time_since_epoch())
                                    .count();
  RequestStreamer streamer(config_->maxDataPointsPerRequest(), config_->resource_attributes(),
                           counter_temporality_, histogram_temporality_, std::move(send_callback),
                           snapshot_time, delta_start_time_ns, cumulative_start_time_ns,
                           config_->enableMetricAggregation());

  if (!config_->enableMetricAggregation()) {
    sinkMetrics(snapshot, streamer);
    streamer.send();
    return;
  }

  MetricAggregator aggregator(counter_temporality_, histogram_temporality_);
  sinkMetrics(snapshot, aggregator);
  streamer.addAggregationResult(aggregator.releaseResult());
  streamer.send();
}

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
