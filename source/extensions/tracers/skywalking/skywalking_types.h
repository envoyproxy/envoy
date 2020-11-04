#pragma once

#include <string>
#include <vector>

#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/http/header_map.h"
#include "envoy/tracing/http_tracer.h"

#include "language-agent/Tracing.pb.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class SegmentContext;
using SegmentContextSharedPtr = std::shared_ptr<SegmentContext>;

class SpanStore;
using SpanStorePtr = std::unique_ptr<SpanStore>;

class SpanContext;
using SpanContextPtr = std::unique_ptr<SpanContext>;

class SpanContext : public Logger::Loggable<Logger::Id::tracing> {
public:
  /*
   * Parse the context of the previous span from the request and decide whether to sample it or
   * not.
   *
   * @param headers The request headers.
   * @return SpanContextPtr The previous span context parsed from request headers.
   */
  static SpanContextPtr spanContextFromRequest(Http::RequestHeaderMap& headers);

  // Sampling flag. This field can only be 0 or 1. 1 means this trace need to be sampled and send to
  // backend.
  int sampled_{0};

  // This span id points to the parent span in parent trace segment.
  int span_id_{0};

  std::string trace_id_;

  // This trace segment id points to the parent trace segment.
  std::string trace_segment_id_;

  std::string service_;
  std::string service_instance_;

  // Operation Name of the first entry span in the parent segment.
  std::string endpoint_;

  // Target address used at client side of this request. The network address(not must be IP + port)
  // used at client side to access this target service.
  std::string target_address_;

private:
  // Private default constructor. We can only create SpanContext by 'spanContextFromRequest'.
  SpanContext() = default;
};

class SegmentContext : public Logger::Loggable<Logger::Id::tracing> {
public:
  /*
   * Create a new segment context based on the previous span context that parsed from request
   * headers.
   *
   * @param previous_span_context The previous span context.
   * @param random_generator The random generator that used to create trace id and segment id.
   * @param decision The tracing decision.
   */
  SegmentContext(SpanContextPtr&& previous_span_context, Tracing::Decision decision,
                 Random::RandomGenerator& random_generator);

  /*
   * Set service name.
   *
   * @param service The service name.
   */
  void setService(const std::string& service) { service_ = service; }

  /*
   * Set service instance name.
   *
   * @param service_instance The service instance name.
   */
  void setServiceInstance(const std::string& service_instance) {
    service_instance_ = service_instance;
  }

  /*
   * Create a new SpanStore object and return its pointer. The ownership of the newly created
   * SpanStore object belongs to the current segment context.
   *
   * @param parent_store The pointer that point to parent SpanStore object.
   * @return SpanStore* The pointer that point to newly created SpanStore object.
   */
  SpanStore* createSpanStore(const SpanStore* parent_store);

  /*
   * Get all SpanStore objects in the current segment.
   */
  const std::vector<SpanStorePtr>& spanList() const { return span_list_; }

  /*
   * Get root SpanStore object in the current segment.
   */
  const SpanStore* rootSpanStore() { return span_list_.empty() ? nullptr : span_list_[0].get(); }

  int sampled() const { return sampled_; }
  const std::string& traceId() const { return trace_id_; }
  const std::string& traceSegmentId() const { return trace_segment_id_; }

  const std::string& service() const { return service_; }
  const std::string& serviceInstance() const { return service_instance_; }

  SpanContext* previousSpanContext() const { return previous_span_context_.get(); }

private:
  int sampled_{0};
  // This value is unique in the entire tracing link. If previous_context is null, we will use
  // random_generator to create a trace id.
  std::string trace_id_;
  // Envoy creates a new span when it accepts a new HTTP request. This span and all of its child
  // spans belong to the same segment and share the segment id.
  std::string trace_segment_id_;

  std::string service_;
  std::string service_instance_;

  // The SegmentContext parsed from the request headers. If no propagation headers in request then
  // this will be nullptr.
  SpanContextPtr previous_span_context_;

  std::vector<SpanStorePtr> span_list_;
};

using Tag = std::pair<std::string, std::string>;

/*
 * A helper class for the SkyWalking span and is used to store all span-related data, including span
 * id, parent span id, tags and so on. Whenever we create a new span, we create a new SpanStore
 * object. The new span will hold a pointer to the newly created SpanStore object and write data to
 * it or get data from it.
 */
class SpanStore : public Logger::Loggable<Logger::Id::tracing> {
public:
  /*
   * Construct a SpanStore object using span context and time source.
   *
   * @param segment_context The pointer that point to current span context. This can not be null.
   * @param time_source A time source to get the span end time.
   */
  explicit SpanStore(SegmentContext* segment_context) : segment_context_(segment_context) {}

  /*
   * Get operation name of span.
   */
  const std::string& operation() const { return operation_; }

  /*
   * Get peer address. The peer in SkyWalking is different with the tag value of 'peer.address'. The
   * tag value of 'peer.address' in Envoy is downstream address and the peer in SkyWalking is
   * upstream address.
   */
  const std::string& peerAddress() const { return peer_address_; }

  /*
   * Get span start time.
   */
  uint64_t startTime() const { return start_time_; }

  /*
   * Get span end time.
   */
  uint64_t endTime() const { return end_time_; }

  /*
   * Get span tags.
   */
  const std::vector<Tag>& tags() const { return tags_; }

  /*
   * Get span logs.
   */
  const std::vector<Log>& logs() const { return logs_; }

  /*
   * Get span sampling flag.
   */
  int sampled() const { return sampled_; }

  /*
   * Get span id.
   */
  int spanId() const { return span_id_; }

  /*
   * Get parent span id.
   */
  int parentSpanId() const { return parent_span_id_; }

  /*
   * Determines if an error has occurred in the current span.
   */
  bool isError() const { return is_error_; }

  /*
   * Determines if the current span is an entry span.
   *
   * Reference:
   * https://github.com/apache/skywalking/blob/v8.1.0/docs/en/protocols/Trace-Data-Protocol-v3.md
   */
  bool isEntrySpan() const { return is_entry_span_; }

  /*
   * Set span start time. This is the time when the HTTP request started, not the time when the span
   * was created.
   */
  void setStartTime(uint64_t start_time) { start_time_ = start_time; }

  /*
   * Set span end time. It is meaningless for now. End time will be set by finish.
   */
  void setEndTime(uint64_t end_time) { end_time_ = end_time; }

  /*
   * Set operation name.
   */
  void setOperation(const std::string& operation) { operation_ = operation; }

  /*
   * Set peer address. In SkyWalking, the peer address is only set in Exit Span. And it should the
   * upstream address. Since the upstream address cannot be obtained at the request stage, the
   * request host is used instead.
   */
  void setPeerAddress(const std::string& peer_address) { peer_address_ = peer_address; }

  /*
   * Set if the current span has an error.
   */
  void setAsError(bool is_error) { is_error_ = is_error; }

  /*
   * Set if the current span is a entry span.
   */
  void setAsEntrySpan(bool is_entry_span) { is_entry_span_ = is_entry_span; }

  /*
   * Add a new tag entry to current span.
   */
  void addTag(absl::string_view name, absl::string_view value) { tags_.emplace_back(name, value); }

  /*
   * Add a new log entry to current span. Due to different data formats, log is temporarily not
   * supported.
   */
  void addLog(SystemTime, const std::string&) {}

  /*
   * Set span id of current span. The span id in each segment is started from 0. When new span is
   * created, its span id is the current max span id plus 1.
   */
  void setSpanId(int span_id) { span_id_ = span_id; }

  /*
   * Set parent span id. Notice that in SkyWalking, the parent span and the child span belong to the
   * same segment. The first span of each segment has a parent span id of -1.
   */
  void setParentSpanId(int parent_span_id) { parent_span_id_ = parent_span_id; }

  /*
   * Set sampling flag. In general, the sampling flag of span is consistent with the current span
   * context.
   */
  void setSampled(int sampled) { sampled_ = sampled == 0 ? 0 : 1; }

  /*
   * Inject current span context information to request headers. This will update original
   * propagation headers.
   *
   * @param request_headers The request headers.
   */
  void injectContext(Http::RequestHeaderMap& request_headers) const;

private:
  SegmentContext* segment_context_{nullptr};

  int sampled_{0};

  int span_id_{0};
  int parent_span_id_{-1};

  uint64_t start_time_{0};
  uint64_t end_time_{0};

  std::string operation_;
  std::string peer_address_;

  bool is_error_{false};
  bool is_entry_span_{true};

  std::vector<Tag> tags_;
  std::vector<Log> logs_;
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
