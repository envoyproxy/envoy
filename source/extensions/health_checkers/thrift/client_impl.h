#pragma once

#include <chrono>

// TODO split the include files
#include "envoy/api/api.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.validate.h"
#include "envoy/extensions/health_checkers/thrift/v3/thrift.pb.h"
#include "envoy/router/router.h"

#include "source/common/network/filter_impl.h"
#include "source/common/upstream/health_checker_base_impl.h"
#include "source/extensions/filters/network/thrift_proxy/config.h"
#include "source/extensions/filters/network/thrift_proxy/decoder.h"
#include "source/extensions/filters/network/thrift_proxy/passthrough_decoder_event_handler.h"
#include "source/extensions/filters/network/thrift_proxy/router/router.h"

#include "source/extensions/health_checkers/thrift/client.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

using namespace Envoy::Extensions::NetworkFilters;
using namespace Envoy::Extensions::NetworkFilters::ThriftProxy;

// The simple response decoder decodes the response and inform the health
// check session if it's a success response or not.
class SimpleResponseDecoder : public DecoderCallbacks,
                              public PassThroughDecoderEventHandler,
                              protected Logger::Loggable<Logger::Id::hc> {
public:
  SimpleResponseDecoder(TransportPtr transport, ProtocolPtr protocol)
      : transport_(std::move(transport)), protocol_(std::move(protocol)),
        decoder_(std::make_unique<Decoder>(*transport_, *protocol_, *this)) {}

  // Return if the response is complete.
  bool onData(Buffer::Instance& data);

  // Check if it is a success response or not.
  bool responseSuccess();

  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
  FilterStatus messageEnd() override;
  FilterStatus transportEnd() override;

  // DecoderCallbacks
  DecoderEventHandler& newDecoderEventHandler() override { return *this; }
  bool passthroughEnabled() const override { return true; };
  bool isRequest() const override { return false; }
  bool headerKeysPreserveCase() const override { return false; };

private:
  TransportPtr transport_;
  ProtocolPtr protocol_;
  DecoderPtr decoder_;
  Buffer::OwnedImpl buffer_;
  absl::optional<bool> success_;
  bool complete_{};
};

using SimpleResponseDecoderPtr = std::unique_ptr<SimpleResponseDecoder>;

class ClientImpl;

// Network::ClientConnection takes a shared pointer callback but we need a
// unique DeferredDeletable pointer for connection management. Therefore we
// need additional wrapper class.
class ThriftSessionCallbacks : public Network::ConnectionCallbacks,
                               public Network::ReadFilterBaseImpl {
public:
  ThriftSessionCallbacks(ClientImpl& parent) : parent_(parent) {}

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool) override;

private:
  ClientImpl& parent_;
};

using ThriftSessionCallbacksSharedPtr = std::shared_ptr<ThriftSessionCallbacks>;

class ClientImpl : public Client,
                   public Network::ConnectionCallbacks,
                   protected Logger::Loggable<Logger::Id::hc> {
public:
  ClientImpl(ClientCallback& callback, TransportType transport, ProtocolType protocol,
             const std::string& method_name, Upstream::HostSharedPtr host, int32_t seq_id)
      : parent_(callback), transport_(transport), protocol_(protocol), method_name_(method_name),
        host_(host), seq_id_(seq_id) {}

  void start() override;
  bool makeRequest() override;
  void close() override;

  // For ThriftSessionCallbacks
  void onData(Buffer::Instance& data);

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override { parent_.onEvent(event); }
  void onAboveWriteBufferHighWatermark() override { parent_.onAboveWriteBufferHighWatermark(); }
  void onBelowWriteBufferLowWatermark() override { parent_.onBelowWriteBufferLowWatermark(); }

private:
  TransportPtr createTransport() {
    return NamedTransportConfigFactory::getFactory(transport_).createTransport();
  }

  ProtocolPtr createProtocol() {
    return NamedProtocolConfigFactory::getFactory(protocol_).createProtocol();
  }

  int32_t sequenceId() {
    if (seq_id_ == std::numeric_limits<int32_t>::max()) {
      seq_id_ = 0;
    }
    seq_id_++;
    return seq_id_;
  }
  ClientCallback& parent_;
  const TransportType transport_;
  const ProtocolType protocol_;
  const std::string& method_name_;
  Upstream::HostSharedPtr host_;
  Network::ClientConnectionPtr connection_;
  Upstream::HostDescriptionConstSharedPtr host_description_;

  int32_t seq_id_{0};
  ThriftSessionCallbacksSharedPtr session_callbacks_;
  SimpleResponseDecoderPtr response_decoder_;
  absl::optional<bool> success_;
};

class ClientFactoryImpl : public ClientFactory {
public:
  // ClientFactory
  ClientPtr create(ClientCallback& callbacks, TransportType transport, ProtocolType protocol,
                   const std::string& method_name, Upstream::HostSharedPtr host,
                   int32_t seq_id) override;

  static ClientFactoryImpl instance_;
};

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
