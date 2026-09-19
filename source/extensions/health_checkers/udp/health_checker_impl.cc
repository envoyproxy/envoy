#include "source/extensions/health_checkers/udp/health_checker_impl.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "envoy/api/io_error.h"
#include "envoy/common/exception.h"
#include "envoy/event/file_event.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/upstream/health_checker_impl.h"

namespace Envoy {
namespace Upstream {

HealthCheckerSharedPtr UdpHealthCheckerFactory::createCustomHealthChecker(
    const envoy::config::core::v3::HealthCheck& config,
    Server::Configuration::HealthCheckerFactoryContext& context) {
  envoy::extensions::health_checkers::udp::v3::UdpHealthCheck udp_config;
  THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
      config.custom_health_check().typed_config(), context.messageValidationVisitor(), udp_config));

  return std::make_shared<ProdUdpHealthCheckerImpl>(
      context.cluster(), config, udp_config, context.mainThreadDispatcher(), context.runtime(),
      context.api().randomGenerator(), context.eventLogger());
}

REGISTER_FACTORY(UdpHealthCheckerFactory, Server::Configuration::CustomHealthCheckerFactory);

namespace {

// Bound repeated EINTR retries so a signal-heavy socket cannot monopolize the dispatcher.
constexpr uint32_t MaxSendAttemptsPerInvocation = 16;

std::vector<uint8_t> decodePayload(const envoy::config::core::v3::HealthCheck::Payload& payload) {
  auto bytes_or_error = PayloadMatcher::loadProtoBytes(payload);
  THROW_IF_NOT_OK_REF(bytes_or_error.status());
  ASSERT(bytes_or_error->size() == 1);
  return std::move(bytes_or_error->front());
}

} // namespace

UdpHealthCheckerImpl::UdpHealthCheckerImpl(
    const Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
    const envoy::extensions::health_checkers::udp::v3::UdpHealthCheck& udp_config,
    Event::Dispatcher& dispatcher, Runtime::Loader& runtime, Random::RandomGenerator& random,
    HealthCheckEventLoggerPtr&& event_logger)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random, std::move(event_logger)),
      send_bytes_(decodePayload(udp_config.send())),
      receive_bytes_(decodePayload(udp_config.receive())) {}

UdpHealthCheckerImpl::ActiveSession::ActiveSession(UdpHealthCheckerImpl& parent,
                                                   const HostSharedPtr& host)
    : ActiveHealthCheckSession(parent, host), parent_(parent),
      deferred_callback_(
          parent.dispatcher_.createSchedulableCallback([this]() -> void { onDeferredAction(); })),
      receive_buffer_(parent.receive_bytes_.size() + 1) {}

void UdpHealthCheckerImpl::ActiveSession::scheduleAction(DeferredAction action) {
  ASSERT(deferred_action_ == DeferredAction::None);
  deferred_action_ = action;
  deferred_callback_->scheduleCallbackNextIteration();
}

void UdpHealthCheckerImpl::ActiveSession::onDeferredAction() {
  const DeferredAction action = deferred_action_;
  deferred_action_ = DeferredAction::None;
  if (!attempt_active_) {
    return;
  }

  switch (action) {
  case DeferredAction::Send:
    sendRequest();
    return;
  case DeferredAction::NetworkFailure:
    completeFailure();
    return;
  case DeferredAction::None:
    return;
  }
}

void UdpHealthCheckerImpl::ActiveSession::onInterval() {
  ASSERT(!attempt_active_);
  ASSERT(socket_ == nullptr);
  ASSERT(deferred_action_ == DeferredAction::None);

  const Network::Address::InstanceConstSharedPtr address = host_->healthCheckAddress();
  attempt_active_ = true;
  request_sent_ = false;
  socket_ = parent_.createSocket(address);

  if (socket_ == nullptr || !socket_->isOpen()) {
    scheduleAction(DeferredAction::NetworkFailure);
    return;
  }

  const auto local_address =
      host_->cluster().getUpstreamLocalAddressSelector()->getUpstreamLocalAddress(address, nullptr,
                                                                                  {});
  if (!Network::Socket::applyOptions(local_address.socket_options_, *socket_,
                                     envoy::config::core::v3::SocketOption::STATE_PREBIND)) {
    scheduleAction(DeferredAction::NetworkFailure);
    return;
  }
  if (local_address.address_ != nullptr) {
    if (local_address.address_->type() != Network::Address::Type::Ip ||
        local_address.address_->ip()->port() != 0) {
      // A fixed local address cannot be rebound while the previous socket is retained. Reusing a
      // fixed port also makes delayed-response isolation impossible, so release it before bind.
      retired_socket_.reset();
    }
    const Api::SysCallIntResult bind_result = socket_->bind(local_address.address_);
    if (bind_result.return_value_ < 0) {
      scheduleAction(DeferredAction::NetworkFailure);
      return;
    }
  }

  const Api::SysCallIntResult connect_result = socket_->connect(address);
  if (connect_result.return_value_ != 0) {
    scheduleAction(DeferredAction::NetworkFailure);
    return;
  }
  // Keep the previous socket open until the new socket has selected its local port. This prevents
  // delayed responses from a previous attempt from being delivered to the new attempt.
  retired_socket_.reset();

  socket_->ioHandle().initializeFileEvent(
      parent_.dispatcher_, [this](uint32_t events) { return onFileEvent(events); },
      Event::PlatformDefaultTriggerType, 0);
  file_event_initialized_ = true;
  scheduleAction(DeferredAction::Send);
}

void UdpHealthCheckerImpl::ActiveSession::sendRequest() {
  ASSERT(attempt_active_);
  ASSERT(socket_ != nullptr);
  ASSERT(!request_sent_);

  for (uint32_t attempt = 0; attempt < MaxSendAttemptsPerInvocation; ++attempt) {
    Api::IoCallUint64Result result =
        socket_->ioHandle().send(parent_.send_bytes_.data(), parent_.send_bytes_.size());
    if (result.ok()) {
      if (result.return_value_ != parent_.send_bytes_.size()) {
        completeFailure();
        return;
      }

      request_sent_ = true;
      socket_->ioHandle().enableFileEvents(Event::FileReadyType::Read |
                                           Event::FileReadyType::Closed);
      return;
    }

    if (result.wouldBlock()) {
      socket_->ioHandle().enableFileEvents(Event::FileReadyType::Write |
                                           Event::FileReadyType::Closed);
      return;
    }
    if (result.err_->getErrorCode() != Api::IoError::IoErrorCode::Interrupt) {
      completeFailure();
      return;
    }
  }

  socket_->ioHandle().enableFileEvents(Event::FileReadyType::Write | Event::FileReadyType::Closed);
  socket_->ioHandle().activateFileEvents(Event::FileReadyType::Write);
}

absl::Status UdpHealthCheckerImpl::ActiveSession::onFileEvent(uint32_t events) {
  if (!attempt_active_) {
    return absl::OkStatus();
  }

  if (events & Event::FileReadyType::Closed) {
    completeFailure();
    return absl::OkStatus();
  }

  if ((events & Event::FileReadyType::Write) && !request_sent_) {
    sendRequest();
  }
  if (attempt_active_ && (events & Event::FileReadyType::Read) && request_sent_) {
    onReadReady();
  }

  return absl::OkStatus();
}

void UdpHealthCheckerImpl::ActiveSession::onReadReady() {
  ASSERT(attempt_active_);
  ASSERT(socket_ != nullptr);
  ASSERT(request_sent_);

  for (uint64_t received = 0; received < Network::NUM_DATAGRAMS_PER_RECEIVE; ++received) {
    Api::IoCallUint64Result result =
        socket_->ioHandle().recv(receive_buffer_.data(), receive_buffer_.size(), 0);
    if (result.ok()) {
      if (result.return_value_ == parent_.receive_bytes_.size() &&
          std::equal(parent_.receive_bytes_.begin(), parent_.receive_bytes_.end(),
                     receive_buffer_.begin())) {
        completeSuccess();
        return;
      }
      continue;
    }

    if (result.wouldBlock()) {
      return;
    }
    const Api::IoError::IoErrorCode error = result.err_->getErrorCode();
    if (error == Api::IoError::IoErrorCode::MessageTooBig) {
      // Windows reports a truncated UDP datagram as WSAEMSGSIZE. The datagram has been consumed and
      // is a non-match, so continue waiting just as on platforms that return the truncated length.
      continue;
    }
    if (error != Api::IoError::IoErrorCode::Interrupt) {
      completeFailure();
      return;
    }
  }

  socket_->ioHandle().activateFileEvents(Event::FileReadyType::Read);
}

void UdpHealthCheckerImpl::ActiveSession::retireSocket() {
  ASSERT(attempt_active_);
  if (socket_ != nullptr) {
    if (file_event_initialized_) {
      socket_->ioHandle().enableFileEvents(0);
    }
    if (retired_socket_ == nullptr) {
      retired_socket_ = std::move(socket_);
    } else {
      // A previous connected socket is retained across setup failures to keep its local port from
      // being reused. The failed replacement has no file event and can be destroyed here.
      ASSERT(!file_event_initialized_);
      socket_.reset();
    }
  }

  attempt_active_ = false;
  file_event_initialized_ = false;
  request_sent_ = false;
}

void UdpHealthCheckerImpl::ActiveSession::completeSuccess() {
  if (!attempt_active_) {
    return;
  }
  retireSocket();
  handleSuccess(false);
}

void UdpHealthCheckerImpl::ActiveSession::completeFailure() {
  if (!attempt_active_) {
    return;
  }
  retireSocket();
  handleFailure(envoy::data::core::v3::NETWORK);
}

void UdpHealthCheckerImpl::ActiveSession::onTimeout() {
  if (deferred_callback_->enabled()) {
    deferred_callback_->cancel();
  }
  deferred_action_ = DeferredAction::None;
  if (attempt_active_) {
    retireSocket();
  }
}

void UdpHealthCheckerImpl::ActiveSession::onDeferredDelete() {
  if (deferred_callback_->enabled()) {
    deferred_callback_->cancel();
  }
  deferred_action_ = DeferredAction::None;
  if (socket_ != nullptr && file_event_initialized_) {
    socket_->ioHandle().enableFileEvents(0);
  }
  attempt_active_ = false;
}

Network::SocketPtr
ProdUdpHealthCheckerImpl::createSocket(const Network::Address::InstanceConstSharedPtr& address) {
  return std::make_unique<Network::SocketImpl>(Network::Socket::Type::Datagram, address, address,
                                               Network::SocketCreationOptions{});
}

} // namespace Upstream
} // namespace Envoy
