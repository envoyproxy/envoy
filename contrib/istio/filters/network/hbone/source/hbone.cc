#include "contrib/istio/filters/network/hbone/source/hbone.h"

#include "envoy/event/file_event.h"

#include "source/common/network/raw_buffer_socket.h"
#include "source/extensions/io_socket/user_space/io_handle.h"

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Istio {
namespace Hbone {
namespace {

constexpr absl::string_view RejectionPrefix = "istio_hbone_connect_rejected:";

std::optional<uint32_t> rejectedStatus(absl::string_view reason, absl::string_view prefix) {
  if (!absl::StartsWith(reason, prefix)) {
    return std::nullopt;
  }
  reason.remove_prefix(prefix.size());
  uint32_t status;
  if (reason.size() != 3 || !absl::SimpleAtoi(reason, &status) || status < 100 ||
      (status >= 500 && status < 600) || status == 200) {
    return std::nullopt;
  }
  return status;
}

StreamInfo::FilterState::Objects withConnectState(StreamInfo::FilterState::Objects objects,
                                                  const ConnectStateSharedPtr& state) {
  objects.push_back(
      {state, StreamInfo::StreamSharingMayImpactPooling::None, std::string(ConnectState::Key)});
  return objects;
}

} // namespace

std::optional<uint32_t> permanentConnectRejection(absl::string_view reason) {
  return rejectedStatus(reason, RejectionPrefix);
}

std::string tunnelFailureReason(absl::string_view reason) {
  if (const auto status = rejectedStatus(reason, "tunnel_response:")) {
    return absl::StrCat(RejectionPrefix, *status);
  }
  return std::string(reason);
}

void ConnectState::notify() {
  if (callback_) {
    callback_();
  }
}

void ConnectState::accept() {
  if (phase_ == Phase::Pending) {
    failure_reason_.clear();
    phase_ = Phase::Accepted;
    notify();
  }
}

void ConnectState::recordAttemptFailure(absl::string_view reason) {
  if (phase_ == Phase::Pending) {
    failure_reason_ = std::string(reason);
  }
}

void ConnectState::fail(absl::string_view reason) {
  // Once readiness has been released, normal stream forwarding owns close and
  // half-close semantics. In particular, do not discard final buffered data.
  if (phase_ == Phase::Connected || phase_ == Phase::Failed) {
    return;
  }
  failure_reason_ = std::string(reason);
  phase_ = Phase::Failed;
  notify();
}

void ConnectState::connected() {
  ASSERT(phase_ == Phase::Accepted);
  phase_ = Phase::Connected;
}

UpstreamSocket::UpstreamSocket(ConnectStateSharedPtr state,
                               std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
                               StreamInfo::FilterState::Objects objects,
                               Network::TransportSocketPtr inner_socket)
    : InternalSocket(inner_socket ? std::move(inner_socket)
                                  : std::make_unique<Network::RawBufferSocket>(),
                     std::move(metadata), withConnectState(std::move(objects), state)),
      state_(std::move(state)) {}

UpstreamSocket::~UpstreamSocket() { cancel(); }

absl::string_view UpstreamSocket::failureReason() const {
  // Keep nested TLS diagnostics when CONNECT itself succeeded.
  return state_->failureReason().empty() ? InternalSocket::failureReason()
                                         : state_->failureReason();
}

void UpstreamSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  InternalSocket::setTransportSocketCallbacks(callbacks);
  callbacks_ = &callbacks;
  ready_callback_ = callbacks.connection().dispatcher().createSchedulableCallback(
      [this]() { completeConnect(); });
  state_->setCallback([this]() { scheduleReady(); });
  if (dynamic_cast<IoSocket::UserSpace::IoHandle*>(&callbacks.ioHandle()) == nullptr) {
    state_->fail("hbone adapter requires an internal listener");
  }
}

Api::SysCallIntResult UpstreamSocket::connect(Network::ConnectionSocket& socket) {
  if (state_->phase() == ConnectState::Phase::Failed) {
    return {-1, SOCKET_ERROR_INVAL};
  }
  return InternalSocket::connect(socket);
}

void UpstreamSocket::onConnected() {
  internal_connected_ = true;
  scheduleReady();
}

void UpstreamSocket::scheduleReady() {
  if (internal_connected_ && ready_callback_ &&
      (state_->phase() == ConnectState::Phase::Accepted ||
       state_->phase() == ConnectState::Phase::Failed)) {
    ready_callback_->scheduleCallbackNextIteration();
  }
}

void UpstreamSocket::completeConnect() {
  if (state_->phase() == ConnectState::Phase::Failed) {
    // Return a read error through ConnectionImpl so the pool sees RemoteClose
    // while still waiting for transport readiness.
    callbacks_->setTransportSocketIsReadable();
    return;
  }
  ASSERT(state_->phase() == ConnectState::Phase::Accepted);
  state_->connected();
  InternalSocket::onConnected();
  if (callbacks_->connection().state() == Network::Connection::State::Open) {
    // A server-first protocol may have sent data while readiness was deferred.
    callbacks_->setTransportSocketIsReadable();
    // Connected only flushes nonempty write buffers; also resume a pending empty half-close.
    callbacks_->ioHandle().activateFileEvents(Event::FileReadyType::Write);
  }
}

Network::IoResult UpstreamSocket::doRead(Buffer::Instance& buffer) {
  if (state_->phase() == ConnectState::Phase::Failed) {
    return {Network::PostIoAction::Close, 0, false};
  }
  if (state_->phase() != ConnectState::Phase::Connected) {
    // Detect a peer close even if the accepting filter was never instantiated.
    auto* io = dynamic_cast<IoSocket::UserSpace::IoHandle*>(&callbacks_->ioHandle());
    if (state_->phase() == ConnectState::Phase::Pending && io != nullptr && io->hasReceivedEof()) {
      state_->fail(state_->failureReason().empty()
                       ? "hbone internal peer closed before CONNECT readiness"
                       : state_->failureReason());
      return {Network::PostIoAction::Close, 0, false};
    }
    return {Network::PostIoAction::KeepOpen, 0, false};
  }
  return InternalSocket::doRead(buffer);
}

Network::IoResult UpstreamSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  if (state_->phase() == ConnectState::Phase::Failed) {
    return {Network::PostIoAction::Close, 0, false};
  }
  if (state_->phase() != ConnectState::Phase::Connected) {
    return {Network::PostIoAction::KeepOpen, 0, false};
  }
  return InternalSocket::doWrite(buffer, end_stream);
}

bool UpstreamSocket::canFlushClose() {
  return state_->phase() == ConnectState::Phase::Connected && InternalSocket::canFlushClose();
}

void UpstreamSocket::cancel() {
  state_->setCallback(nullptr);
  state_->fail("hbone internal connection cancelled before CONNECT readiness");
  if (ready_callback_) {
    ready_callback_->cancel();
  }
}

void UpstreamSocket::closeSocket(Network::ConnectionEvent event, bool abort_reset) {
  cancel();
  InternalSocket::closeSocket(event, abort_reset);
}

} // namespace Hbone
} // namespace Istio
} // namespace Extensions
} // namespace Envoy
