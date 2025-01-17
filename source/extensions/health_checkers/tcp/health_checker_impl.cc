#include "source/extensions/health_checkers/tcp/health_checker_impl.h"

#include <cstdint>
#include <iterator>
#include <memory>

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/server/health_checker_config.h"
#include "envoy/type/v3/http.pb.h"
#include "envoy/type/v3/range.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/macros.h"
#include "source/common/config/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/utility.h"
#include "source/common/router/router.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/upstream/host_utility.h"
#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Upstream {

Upstream::HealthCheckerSharedPtr TcpHealthCheckerFactory::createCustomHealthChecker(
    const envoy::config::core::v3::HealthCheck& config,
    Server::Configuration::HealthCheckerFactoryContext& context) {
  return std::make_shared<TcpHealthCheckerImpl>(
      context.cluster(), config, context.mainThreadDispatcher(), context.runtime(),
      context.api().randomGenerator(), context.eventLogger());
}

REGISTER_FACTORY(TcpHealthCheckerFactory, Server::Configuration::CustomHealthCheckerFactory);

TcpHealthCheckerImpl::TcpHealthCheckerImpl(const Cluster& cluster,
                                           const envoy::config::core::v3::HealthCheck& config,
                                           Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                                           Random::RandomGenerator& random,
                                           HealthCheckEventLoggerPtr&& event_logger)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random, std::move(event_logger)),
      send_bytes_([&config] {
        Protobuf::RepeatedPtrField<envoy::config::core::v3::HealthCheck::Payload> send_repeated;
        if (!config.tcp_health_check().send().text().empty()) {
          send_repeated.Add()->CopyFrom(config.tcp_health_check().send());
        }
        auto bytes_or_error = PayloadMatcher::loadProtoBytes(send_repeated);
        THROW_IF_NOT_OK_REF(bytes_or_error.status());
        return bytes_or_error.value();
      }()),
      proxy_protocol_config_(config.tcp_health_check().has_proxy_protocol_config()
                                 ? std::make_unique<envoy::config::core::v3::ProxyProtocolConfig>(
                                       config.tcp_health_check().proxy_protocol_config())
                                 : nullptr) {
  auto bytes_or_error = PayloadMatcher::loadProtoBytes(config.tcp_health_check().receive());
  THROW_IF_NOT_OK_REF(bytes_or_error.status());
  receive_bytes_ = bytes_or_error.value();
}

TcpHealthCheckerImpl::TcpActiveHealthCheckSession::~TcpActiveHealthCheckSession() {
  ASSERT(client_ == nullptr);
}

void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onDeferredDelete() {
  if (client_) {
    expect_close_ = true;
    client_->close(Network::ConnectionCloseType::Abort);
  }
}

void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onData(Buffer::Instance& data) {
  ENVOY_CONN_LOG(debug, "hc tcp total pending buffer={}", *client_, data.length());
  // TODO(lilika): The TCP health checker does generic pattern matching so we can't differentiate
  // between wrong data and not enough data. We could likely do better here and figure out cases in
  // which a match is not possible but that is not done now.
  if (PayloadMatcher::match(parent_.receive_bytes_, data)) {
    ENVOY_CONN_LOG(debug, "hc tcp healthcheck passed, health_check_address={}", *client_,
                   host_->healthCheckAddress()->asString());
    data.drain(data.length());
    handleSuccess(false);
    if (!parent_.reuse_connection_) {
      expect_close_ = true;
      client_->close(Network::ConnectionCloseType::Abort);
    }
  }
}

void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    if (!expect_close_) {
      ENVOY_CONN_LOG(debug, "hc tcp connection unexpected closed, health_check_address={}",
                     *client_, host_->healthCheckAddress()->asString());
      handleFailure(envoy::data::core::v3::NETWORK);
    }
    parent_.dispatcher_.deferredDelete(std::move(client_));
  }

  if (event == Network::ConnectionEvent::Connected && parent_.receive_bytes_.empty()) {
    // In this case we are just testing that we can connect, so immediately succeed. Also, since
    // we are just doing a connection test, close the connection.
    // NOTE(mattklein123): I've seen cases where the kernel will report a successful connection, and
    // then proceed to fail subsequent calls (so the connection did not actually succeed). I'm not
    // sure what situations cause this. If this turns into a problem, we may need to introduce a
    // timer and see if the connection stays alive for some period of time while waiting to read.
    // (Though we may never get a FIN and won't know until if/when we try to write). In short, this
    // may need to get more complicated but we can start here.
    // TODO(mattklein123): If we had a way on the connection interface to do an immediate read (vs.
    // evented), that would be a good check to run here to make sure it returns the equivalent of
    // EAGAIN. Need to think through how that would look from an interface perspective.
    // TODO(mattklein123): In the case that a user configured bytes to write, they will not be
    // be written, since we currently have no way to know if the bytes actually get written via
    // the connection interface. We might want to figure out how to handle this better later.
    ENVOY_CONN_LOG(debug, "hc tcp healthcheck passed, health_check_address={}", *client_,
                   host_->healthCheckAddress()->asString());
    expect_close_ = true;
    client_->close(Network::ConnectionCloseType::Abort);
    handleSuccess(false);
  }
}

// TODO(lilika) : Support connection pooling
void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onInterval() {
  if (!client_) {
    client_ =
        host_
            ->createHealthCheckConnection(parent_.dispatcher_, parent_.transportSocketOptions(),
                                          parent_.transportSocketMatchMetadata().get())
            .connection_;
    session_callbacks_ = std::make_shared<TcpSessionCallbacks>(*this);
    client_->addConnectionCallbacks(*session_callbacks_);
    client_->addReadFilter(session_callbacks_);

    expect_close_ = false;
    client_->connect();
    client_->noDelay(true);
  }

  Buffer::OwnedImpl data;
  bool should_write_data = false;

  if (parent_.proxy_protocol_config_ != nullptr) {
    if (parent_.proxy_protocol_config_->version() ==
        envoy::config::core::v3::ProxyProtocolConfig::V1) {
      auto src_addr = client_->connectionInfoProvider().localAddress()->ip();
      auto dst_addr = client_->connectionInfoProvider().remoteAddress()->ip();
      Extensions::Common::ProxyProtocol::generateV1Header(*src_addr, *dst_addr, data);
    } else if (parent_.proxy_protocol_config_->version() ==
               envoy::config::core::v3::ProxyProtocolConfig::V2) {
      Extensions::Common::ProxyProtocol::generateV2LocalHeader(data);
    }
    should_write_data = true;
  }
  if (!parent_.send_bytes_.empty()) {
    for (const std::vector<uint8_t>& segment : parent_.send_bytes_) {
      data.add(segment.data(), segment.size());
    }
    should_write_data = true;
  }
  if (should_write_data) {
    client_->write(data, false);
  }
}

void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onTimeout() {
  ENVOY_CONN_LOG(debug, "hc tcp connection timeout, health_flags={}, health_check_address={}",
                 *client_, HostUtility::healthFlagsToString(*host_),
                 host_->healthCheckAddress()->asString());
  expect_close_ = true;
  client_->close(Network::ConnectionCloseType::Abort);
}

} // namespace Upstream
} // namespace Envoy
