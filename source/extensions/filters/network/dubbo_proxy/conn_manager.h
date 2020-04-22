#pragma once

#include "envoy/common/time.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/timespan.h"

#include "common/common/logger.h"

#include "extensions/filters/network/dubbo_proxy/active_message.h"
#include "extensions/filters/network/dubbo_proxy/decoder.h"
#include "extensions/filters/network/dubbo_proxy/decoder_event_handler.h"
#include "extensions/filters/network/dubbo_proxy/filters/filter.h"
#include "extensions/filters/network/dubbo_proxy/protocol.h"
#include "extensions/filters/network/dubbo_proxy/serializer.h"
#include "extensions/filters/network/dubbo_proxy/stats.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

/**
 * Config is a configuration interface for ConnectionManager.
 */
class Config {
public:
  virtual ~Config() = default;

  virtual DubboFilters::FilterChainFactory& filterFactory() PURE;
  virtual DubboFilterStats& stats() PURE;
  virtual ProtocolPtr createProtocol() PURE;
  virtual Router::Config& routerConfig() PURE;
};

// class ActiveMessagePtr;
class ConnectionManager : public Network::ReadFilter,
                          public Network::ConnectionCallbacks,
                          public RequestDecoderCallbacks,
                          Logger::Loggable<Logger::Id::dubbo> {
public:
  using ConfigProtocolType = envoy::extensions::filters::network::dubbo_proxy::v3::ProtocolType;
  using ConfigSerializationType =
      envoy::extensions::filters::network::dubbo_proxy::v3::SerializationType;

  ConnectionManager(Config& config, Runtime::RandomGenerator& random_generator,
                    TimeSource& time_system);
  ~ConnectionManager() override = default;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

  // RequestDecoderCallbacks
  StreamHandler& newStream() override;
  void onHeartbeat(MessageMetadataSharedPtr metadata) override;

  DubboFilterStats& stats() const { return stats_; }
  Network::Connection& connection() const { return read_callbacks_->connection(); }
  TimeSource& time_system() const { return time_system_; }
  Runtime::RandomGenerator& random_generator() const { return random_generator_; }
  Config& config() const { return config_; }
  SerializationType downstreamSerializationType() const { return protocol_->serializer()->type(); }
  ProtocolType downstreamProtocolType() const { return protocol_->type(); }

  void continueDecoding();
  void deferredMessage(ActiveMessage& message);
  void sendLocalReply(MessageMetadata& metadata, const DubboFilters::DirectResponse& response,
                      bool end_stream);

  // This function is for testing only.
  std::list<ActiveMessagePtr>& getActiveMessagesForTest() { return active_message_list_; }

private:
  void dispatch();
  void resetAllMessages(bool local_reset);

  Buffer::OwnedImpl request_buffer_;
  std::list<ActiveMessagePtr> active_message_list_;

  bool stopped_{false};
  bool half_closed_{false};

  Config& config_;
  TimeSource& time_system_;
  DubboFilterStats& stats_;
  Runtime::RandomGenerator& random_generator_;

  SerializerPtr serializer_;
  ProtocolPtr protocol_;
  RequestDecoderPtr decoder_;
  Network::ReadFilterCallbacks* read_callbacks_{};
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
