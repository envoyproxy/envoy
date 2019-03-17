#pragma once

#include "envoy/common/time.h"
#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/dubbo_proxy.pb.h"
#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/dubbo_proxy.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/timespan.h"

#include "common/common/logger.h"

#include "extensions/filters/network/dubbo_proxy/decoder.h"
#include "extensions/filters/network/dubbo_proxy/deserializer.h"
#include "extensions/filters/network/dubbo_proxy/protocol.h"
#include "extensions/filters/network/dubbo_proxy/stats.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class Filter : public Network::Filter,
               public Network::ConnectionCallbacks,
               public ProtocolCallbacks,
               public DecoderCallbacks,
               Logger::Loggable<Logger::Id::dubbo> {
public:
  using ConfigProtocolType = envoy::config::filter::network::dubbo_proxy::v2alpha1::ProtocolType;
  using ConfigSerializationType =
      envoy::config::filter::network::dubbo_proxy::v2alpha1::SerializationType;

  Filter(const std::string& stat_prefix, ConfigProtocolType protocol_type,
         ConfigSerializationType serialization_type, Stats::Scope& scope, TimeSource& time_source);
  virtual ~Filter();

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

  // ProtocolCallbacks
  void onRequestMessage(RequestMessagePtr&& message) override;
  void onResponseMessage(ResponseMessagePtr&& message) override;

  // DecoderCallbacks
  void onRpcInvocation(RpcInvocationPtr&& invo) override;
  void onRpcResult(RpcResultPtr&& res) override;

private:
  DecoderPtr createDecoder(ProtocolCallbacks& prot_callback);
  ProtocolPtr createProtocol(ProtocolCallbacks& callback);
  DeserializerPtr createDeserializer();

  // ActiveMessage tracks downstream requests for which no response has been received.
  struct ActiveMessage {
    ActiveMessage(Filter& parent, int32_t request_id)
        : parent_(parent), request_timer_(new Stats::Timespan(parent_.stats_.request_time_ms_,
                                                              parent_.time_source_)),
          request_id_(request_id) {
      parent_.stats_.request_active_.inc();
    }
    ~ActiveMessage() {
      parent_.stats_.request_active_.dec();
      request_timer_->complete();
    }

    Filter& parent_;
    Stats::TimespanPtr request_timer_;
    const int32_t request_id_;
    absl::optional<bool> success_{};
  };
  typedef std::unique_ptr<ActiveMessage> ActiveMessagePtr;

  // Downstream request decoder, callbacks, and buffer.
  DecoderPtr request_decoder_;
  Buffer::OwnedImpl request_buffer_;

  // Upstream response decoder, callbacks, and buffer.
  DecoderPtr response_decoder_;
  Buffer::OwnedImpl response_buffer_;

  // List of active request messages.
  std::unordered_map<int64_t, ActiveMessagePtr> active_call_map_;

  bool sniffing_{true};
  DubboFilterStats stats_;

  ProtocolType protocol_type_;
  SerializationType serialization_type_;

  TimeSource& time_source_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
