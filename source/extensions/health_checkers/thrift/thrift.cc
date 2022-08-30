#include "source/extensions/health_checkers/thrift/thrift.h"

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.validate.h"
#include "envoy/extensions/health_checkers/thrift/v3/thrift.pb.h"

#include "source/extensions/filters/network/thrift_proxy/thrift.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

namespace {

// Helper functions to get the correct hostname for an L7 health check.
const std::string& getHostname(const Upstream::HostSharedPtr& host,
                               const Upstream::ClusterInfoConstSharedPtr& cluster) {
  if (!host->hostnameForHealthChecks().empty()) {
    return host->hostnameForHealthChecks();
  }

  return cluster->name();
}

} // namespace

ThriftHealthChecker::ThriftHealthChecker(
    const Upstream::Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
    const envoy::extensions::health_checkers::thrift::v3::Thrift& thrift_config,
    Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
    Upstream::HealthCheckEventLoggerPtr&& event_logger, Api::Api& api)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, api.randomGenerator(),
                            std::move(event_logger)),
      method_name_(thrift_config.method_name()),
      transport_(TransportNames::get().getTypeFromProto(thrift_config.transport())),
      protocol_(ProtocolNames::get().getTypeFromProto(thrift_config.protocol())) {
  if (transport_ == TransportType::Auto || protocol_ == ProtocolType::Auto ||
      protocol_ == ProtocolType::Twitter) {
    throw EnvoyException(
        fmt::format("Invalid thrift health check configuration: {}", thrift_config.DebugString()));
  }
}

// ThriftHealthChecker::SimpleResponseDecoder
bool ThriftHealthChecker::SimpleResponseDecoder::onData(Buffer::Instance& data) {
  buffer_.move(data);

  bool underflow = false;
  decoder_->onData(buffer_, underflow);
  ASSERT(complete_ || underflow);

  return complete_;
}

bool ThriftHealthChecker::SimpleResponseDecoder::responseSuccess() {
  ENVOY_LOG(trace, "SimpleResponseDecoder responseSuccess complete={} success={}", complete_,
            success_.has_value() && success_.value());
  return complete_ && success_.has_value() && success_.value();
}

FilterStatus
ThriftHealthChecker::SimpleResponseDecoder::messageBegin(MessageMetadataSharedPtr metadata) {
  ENVOY_LOG(trace, "SimpleResponseDecoder messageBegin message_type={} reply_type={}",
            metadata->hasMessageType() ? MessageTypeNames::get().fromType(metadata->messageType())
                                       : "-",
            metadata->hasReplyType() ? ReplyTypeNames::get().fromType(metadata->replyType()) : "-");

  if (metadata->hasReplyType()) {
    success_ = metadata->replyType() == ReplyType::Success;
  }

  if (metadata->hasMessageType() && metadata->messageType() == MessageType::Exception) {
    success_ = false;
  }
  return FilterStatus::Continue;
}

FilterStatus ThriftHealthChecker::SimpleResponseDecoder::messageEnd() {
  ENVOY_LOG(trace, "SimpleResponseDecoder messageEnd");
  complete_ = true;
  return FilterStatus::Continue;
}

FilterStatus ThriftHealthChecker::SimpleResponseDecoder::transportEnd() {
  ENVOY_LOG(trace, "SimpleResponseDecoder transportEnd");
  return FilterStatus::Continue;
}

// ThriftHealthChecker::Client
void ThriftHealthChecker::Client::start() {
  ENVOY_CONN_LOG(trace, "ThriftHealthChecker Client start", *connection_);
  session_callbacks_ = std::make_unique<ThriftSessionCallbacks>(*this);
  connection_->addConnectionCallbacks(*session_callbacks_);
  connection_->addReadFilter(session_callbacks_);

  connection_->connect();
  connection_->noDelay(true);
}

bool ThriftHealthChecker::Client::makeRequest() {
  ENVOY_CONN_LOG(trace, "ThriftHealthChecker Client makeRequest", *connection_);
  if (connection_->state() == Network::Connection::State::Closed) {
    ENVOY_CONN_LOG(debug, "ThriftHealthChecker Client makeRequest fail due to closed connection",
                   *connection_);
    return false;
  }

  auto& health_checker = parent_.parent_;
  Buffer::OwnedImpl request_buffer;
  ProtocolConverterSharedPtr protocol_converter = std::make_shared<ProtocolConverter>();
  ProtocolPtr protocol = createProtocol();
  protocol_converter->initProtocolConverter(*protocol, request_buffer);

  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
  metadata->setProtocol(health_checker.protocol_);
  metadata->setMethodName(health_checker.method_name_);
  metadata->setMessageType(MessageType::Call);
  metadata->setSequenceId(sequenceId());

  protocol_converter->messageBegin(metadata);
  protocol_converter->structBegin("");
  FieldType field_type_stop = FieldType::Stop;
  int16_t field_id = 0;
  protocol_converter->fieldBegin("", field_type_stop, field_id);
  protocol_converter->structEnd();
  protocol_converter->messageEnd();

  TransportPtr transport = createTransport();
  Buffer::OwnedImpl transport_buffer;
  transport->encodeFrame(transport_buffer, *metadata, request_buffer);

  connection_->write(transport_buffer, false);
  ENVOY_CONN_LOG(trace, "ThriftHealthChecker Client makeRequest success id = {}", *connection_,
                 metadata->sequenceId());
  return true;
}

void ThriftHealthChecker::Client::close() {
  ENVOY_CONN_LOG(trace, "ThriftHealthChecker Client close", *connection_);
  connection_->close(Network::ConnectionCloseType::NoFlush);
}

void ThriftHealthChecker::Client::onData(Buffer::Instance& data) {
  ENVOY_CONN_LOG(trace, "ThriftHealthChecker Client onData. total pending buffer={}", *connection_,
                 data.length());
  if (!response_decoder_) {
    response_decoder_ =
        std::make_unique<SimpleResponseDecoder>(createTransport(), createProtocol());
  }
  if (response_decoder_->onData(data)) {
    ENVOY_CONN_LOG(trace, "Response complete. Result={} ", *connection_,
                   response_decoder_->responseSuccess());
    parent_.onResponseResult(response_decoder_->responseSuccess());
  }
}

// ThriftActiveHealthCheckSession:
ThriftHealthChecker::ThriftActiveHealthCheckSession::ThriftActiveHealthCheckSession(
    ThriftHealthChecker& parent, const Upstream::HostSharedPtr& host)
    : ActiveHealthCheckSession(parent, host), parent_(parent),
      hostname_(getHostname(host, parent_.cluster_.info())) {
  ENVOY_LOG(trace, "ThriftActiveHealthCheckSession construct hostname={}", hostname_);
}

ThriftHealthChecker::ThriftActiveHealthCheckSession::~ThriftActiveHealthCheckSession() {
  ENVOY_LOG(trace, "ThriftActiveHealthCheckSession destruct");
  ASSERT(client_ == nullptr);
}

void ThriftHealthChecker::ThriftActiveHealthCheckSession::onDeferredDelete() {
  if (client_) {
    expect_close_ = true;
    client_->close();
  }
}

void ThriftHealthChecker::ThriftActiveHealthCheckSession::onInterval() {
  ENVOY_LOG(trace, "ThriftActiveHealthCheckSession onInterval");
  if (!client_) {
    ENVOY_LOG(trace, "ThriftActiveHealthCheckSession construct client");
    Upstream::Host::CreateConnectionData conn_data =
        host_->createHealthCheckConnection(parent_.dispatcher_, parent_.transportSocketOptions(),
                                           parent_.transportSocketMatchMetadata().get());
    client_ = std::make_unique<Client>(*this, conn_data, parent_.random_.random());
    client_->start();
    expect_close_ = false;
  }

  client_->makeRequest();
}

void ThriftHealthChecker::ThriftActiveHealthCheckSession::onTimeout() {
  expect_close_ = true;
  client_->close();
}

// Network::ConnectionCallbacks
void ThriftHealthChecker::ThriftActiveHealthCheckSession::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_LOG(trace, "on event close, is_local_close={} expect_close={}",
              event == Network::ConnectionEvent::LocalClose, expect_close_);
    // TODO: check if response is partial or complete
    if (!expect_close_) {
      handleFailure(envoy::data::core::v3::NETWORK);
    }
    // Report failure if the connection was closed without receiving a full response.
    parent_.dispatcher_.deferredDelete(std::move(client_));
  }
}

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
