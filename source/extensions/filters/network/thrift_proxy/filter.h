#pragma once

#include <unordered_map>

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/timespan.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "extensions/filters/network/thrift_proxy/decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * All thrift filter stats. @see stats_macros.h
 */
// clang-format off
#define ALL_THRIFT_FILTER_STATS(COUNTER, GAUGE, HISTOGRAM)                                         \
  COUNTER(request)                                                                                 \
  COUNTER(request_call)                                                                            \
  COUNTER(request_oneway)                                                                          \
  COUNTER(request_invalid_type)                                                                    \
  GAUGE(request_active)                                                                            \
  COUNTER(request_decoding_error)                                                                  \
  HISTOGRAM(request_time_ms)                                                                       \
  COUNTER(response)                                                                                \
  COUNTER(response_reply)                                                                          \
  COUNTER(response_success)                                                                        \
  COUNTER(response_error)                                                                          \
  COUNTER(response_exception)                                                                      \
  COUNTER(response_invalid_type)                                                                   \
  COUNTER(response_decoding_error)                                                                 \
  COUNTER(cx_destroy_local_with_active_rq)                                                         \
  COUNTER(cx_destroy_remote_with_active_rq)
// clang-format on

/**
 * Struct definition for all mongo proxy stats. @see stats_macros.h
 */
struct ThriftFilterStats {
  ALL_THRIFT_FILTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

/**
 * A sniffing filter for thrift traffic.
 */
class Filter : public Network::Filter,
               public Network::ConnectionCallbacks,
               Logger::Loggable<Logger::Id::thrift> {
public:
  Filter(const std::string& stat_prefix, Stats::Scope& scope);
  ~Filter();

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) override {}

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  // RequestCallbacks handles callbacks related to decoding downstream requests.
  class RequestCallbacks : public virtual ProtocolCallbacks, public virtual TransportCallbacks {
  public:
    RequestCallbacks(Filter& parent) : parent_(parent) {}

    // TransportCallbacks
    void transportFrameStart(absl::optional<uint32_t> size) override;
    void transportFrameComplete() override;

    // ProtocolCallbacks
    void messageStart(const absl::string_view name, MessageType msg_type, int32_t seq_id) override;
    void structBegin(const absl::string_view name) override;
    void structField(const absl::string_view name, FieldType field_type, int16_t field_id) override;
    void structEnd() override;
    void messageComplete() override;

  private:
    Filter& parent_;
  };

  // ResponseCallbacks handles callbacks related to decoding upstream responses.
  class ResponseCallbacks : public virtual ProtocolCallbacks, public virtual TransportCallbacks {
  public:
    ResponseCallbacks(Filter& parent) : parent_(parent) {}

    // TransportCallbacks
    void transportFrameStart(absl::optional<uint32_t> size) override;
    void transportFrameComplete() override;

    // ProtocolCallbacks
    void messageStart(const absl::string_view name, MessageType msg_type, int32_t seq_id) override;
    void structBegin(const absl::string_view name) override;
    void structField(const absl::string_view name, FieldType field_type, int16_t field_id) override;
    void structEnd() override;
    void messageComplete() override;

  private:
    Filter& parent_;
    int depth_{0};
  };

  // ActiveMessage tracks downstream requests for which no response has been received.
  struct ActiveMessage {
    ActiveMessage(Filter& parent, MessageType msg_type, int32_t seq_id)
        : parent_(parent), request_timer_(new Stats::Timespan(parent_.stats_.request_time_ms_)),
          msg_type_(msg_type), seq_id_(seq_id) {
      parent_.stats_.request_active_.inc();
    }
    ~ActiveMessage() {
      request_timer_->complete();
      parent_.stats_.request_active_.dec();
    }

    Filter& parent_;
    Stats::TimespanPtr request_timer_;
    const MessageType msg_type_;
    const int32_t seq_id_;
    MessageType response_msg_type_{};
    absl::optional<bool> success_{};
  };
  typedef std::unique_ptr<ActiveMessage> ActiveMessagePtr;

  ThriftFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return ThriftFilterStats{ALL_THRIFT_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                                     POOL_GAUGE_PREFIX(scope, prefix),
                                                     POOL_HISTOGRAM_PREFIX(scope, prefix))};
  }

  void chargeDownstreamRequestStart(MessageType msg_type, int32_t seq_id);
  void chargeDownstreamRequestComplete();
  void chargeUpstreamResponseStart(MessageType msg_type, int32_t seq_id);
  void chargeUpstreamResponseField(FieldType field_type, int16_t field_id);
  void chargeUpstreamResponseComplete();

  // Downstream request decoder, callbacks, and buffer.
  DecoderPtr req_decoder_{};
  RequestCallbacks req_callbacks_;
  Buffer::OwnedImpl req_buffer_;
  // Request currently being decoded.
  ActiveMessagePtr req_;

  // Upstream response decoder, callbacks, and buffer.
  DecoderPtr resp_decoder_{};
  ResponseCallbacks resp_callbacks_;
  Buffer::OwnedImpl resp_buffer_;
  // Response currently being decoded.
  ActiveMessagePtr resp_;

  // List of active request messages.
  std::unordered_map<int32_t, ActiveMessagePtr> active_call_map_;

  bool sniffing_{true};
  ThriftFilterStats stats_;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
