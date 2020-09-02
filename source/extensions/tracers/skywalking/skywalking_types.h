#pragma once

#include <stdint.h>

#include <string>

#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class Endpoint {
public:
  explicit Endpoint(Http::RequestHeaderMap& request_headers)
      : host_(request_headers.getHostValue()),
        method_and_path_(
            absl::StrCat("/", request_headers.getMethodValue(), request_headers.getPathValue())) {}

  const std::string& methodAndPath() const { return method_and_path_; }
  const std::string& host() const { return host_; }

private:
  const std::string host_;
  const std::string method_and_path_;
};

class SpanContext {

public:
  void initialize(Random::RandomGenerator& random_generator);
  bool extract(Http::RequestHeaderMap& request_headers);
  void inject(Http::RequestHeaderMap& request_headers) const;

  void setSampled(bool sampled) { sampled_ = sampled ? 1 : 0; }
  void setParentSpanId(int parent_span_id) { parent_span_id_ = parent_span_id; }
  void setTraceId(const std::string& trace_id) { trace_id_ = trace_id; }
  void setTraceSegmentId(const std::string& trace_segment_id) {
    trace_segment_id_ = trace_segment_id;
  }
  void setService(const std::string& service) { service_ = service; }
  void setServiceInstance(const std::string& service_instance) {
    service_instance_ = service_instance;
  }
  void setParentEndpointAndNetworkAddressUsedAtPeer(const Endpoint& endpoint) {
    parent_endpoint_ = endpoint.methodAndPath();
    network_address_used_at_peer_ = endpoint.host();
  }

  int sampled() const { return sampled_; }
  const std::string& traceId() const { return trace_id_; }
  const std::string& traceSegmentId() const { return trace_segment_id_; }
  int parentSpanId() const { return parent_span_id_; }
  const std::string& service() const { return service_; }
  const std::string& serviceInstance() const { return service_instance_; }
  const std::string& parentEndpoint() const { return parent_endpoint_; }
  const std::string& networkAddressUsedAtPeer() const { return network_address_used_at_peer_; }

  bool isNew() const { return is_new_; }

private:
  int sampled_{1};
  int parent_span_id_{0};

  std::string trace_id_;
  std::string trace_segment_id_;
  std::string service_;
  std::string service_instance_;

  std::string parent_endpoint_;
  // The address used for calling this endpoint.
  std::string network_address_used_at_peer_;

  bool is_new_{true};
};

using Tag = std::pair<std::string, std::string>;

struct Log {
  uint64_t timestamp_;
  std::vector<Tag> data_;
};

class SpanObject {

public:
  explicit SpanObject(const SpanContext& span_context, const SpanContext& previous_span_context,
                      TimeSource& time_source, Random::RandomGenerator& random_generator)
      : span_context_(span_context), previous_span_context_(previous_span_context),
        time_source_(time_source) {
    if (!previous_span_context.isNew()) {
      span_context_.setTraceId(previous_span_context.traceId());
    }
    span_context_.initialize(random_generator);
  }

  void finish();

  void setSpanId(int32_t span_id) { span_id_ = span_id; }
  void setParentSpanId(int32_t parent_span_id) { parent_span_id_ = parent_span_id; }
  void setStartTime(uint64_t start_time) { start_time_ = start_time; }
  void setEndTime(uint64_t end_time) { end_time_ = end_time; }
  void setOperationName(const std::string& operation_name) { operation_name_ = operation_name; }
  void setAsError(bool is_error) { is_error_ = is_error; }
  void setAsEntrySpan(bool is_entry_span) { is_entry_span_ = is_entry_span; }
  void setPeer(const std::string& peer) { peer_ = peer; }
  void addTag(const Tag& tag) { tags_.push_back(tag); }
  void addLog(const Log& log) { logs_.push_back(log); }

  const SpanContext& context() const { return span_context_; }
  const SpanContext& previousContext() const { return previous_span_context_; }

  const std::string& operationName() const { return operation_name_; }
  uint64_t startTime() const { return start_time_; }
  uint64_t endTime() const { return end_time_; }
  bool isError() const { return is_error_; }
  const std::vector<Tag>& tags() const { return tags_; }
  const std::vector<Log>& logs() const { return logs_; }
  int32_t spanId() const { return span_id_; }
  int32_t parentSpanId() const { return parent_span_id_; }
  const std::string& peer() const { return peer_; }

  bool isEntrySpan() const { return is_entry_span_; }
  const std::vector<Tag>& tags() { return tags_; };
  const std::vector<Log>& logs() { return logs_; };

private:
  SpanContext span_context_;
  SpanContext previous_span_context_;

  int32_t span_id_{0};
  int32_t parent_span_id_{-1};

  uint64_t start_time_;
  uint64_t end_time_;

  std::string operation_name_;
  std::string peer_;

  bool is_error_{false};

  bool is_entry_span_{true};

  std::vector<Tag> tags_;
  std::vector<Log> logs_;

  TimeSource& time_source_;
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
