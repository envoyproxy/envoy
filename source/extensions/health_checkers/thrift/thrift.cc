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

// ThriftHealthChecker::Client
bool ThriftHealthChecker::Client::makeRequest() {
  // TODO ENVOY_CONN_LOG(trace, )

  auto& health_checker = parent_.parent_;
  Buffer::OwnedImpl request_buffer;
  ProtocolConverterSharedPtr protocol_converter = std::make_shared<ProtocolConverter>();
  ProtocolPtr protocol =
      NamedProtocolConfigFactory::getFactory(health_checker.protocol_).createProtocol();
  protocol_converter->initProtocolConverter(*protocol, request_buffer);

  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
  metadata->setProtocol(health_checker.protocol_);
  metadata->setMethodName(health_checker.method_name_);
  metadata->setMessageType(MessageType::Call);
  metadata->setSequenceId(sequenceId());

  protocol_converter->messageBegin(metadata);
  protocol_converter->messageEnd();

  // TODO send request
  return true;
}
void ThriftHealthChecker::Client::start() {
  session_callbacks_ = std::make_unique<ThriftSessionCallbacks>(*this);
  connection_->addConnectionCallbacks(*session_callbacks_);
  connection_->addReadFilter(session_callbacks_);

  connection_->connect();
  connection_->noDelay(true);
}

void ThriftHealthChecker::Client::close() {
  connection_->close(Network::ConnectionCloseType::NoFlush);
}

void ThriftHealthChecker::Client::onData(Buffer::Instance& data) {
  // TODO: Response Decoder
  UNREFERENCED_PARAMETER(data);
}

// ThriftActiveHealthCheckSession:
ThriftHealthChecker::ThriftActiveHealthCheckSession::ThriftActiveHealthCheckSession(
    ThriftHealthChecker& parent, const Upstream::HostSharedPtr& host)
    : ActiveHealthCheckSession(parent, host), parent_(parent),
      hostname_(getHostname(host, parent_.cluster_.info())) {}

ThriftHealthChecker::ThriftActiveHealthCheckSession::~ThriftActiveHealthCheckSession() {
  ASSERT(client_ == nullptr);
}

void ThriftHealthChecker::ThriftActiveHealthCheckSession::onDeferredDelete() {
  if (client_) {
    expect_close_ = true;
    client_->close();
  }
}

void ThriftHealthChecker::ThriftActiveHealthCheckSession::onInterval() {
  if (!client_) {
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
