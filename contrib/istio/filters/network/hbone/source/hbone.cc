#include "contrib/istio/filters/network/hbone/source/hbone.h"

#include "envoy/stream_info/bool_accessor.h"

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

void ConnectState::notify() {
  if (callback_) {
    callback_();
  }
}

void ConnectState::accept() {
  if (phase_ == Phase::Pending) {
    phase_ = Phase::Accepted;
    notify();
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
  // A server-first protocol may have sent data while readiness was deferred.
  if (callbacks_->connection().state() == Network::Connection::State::Open) {
    callbacks_->setTransportSocketIsReadable();
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
      state_->fail("hbone internal peer closed before CONNECT readiness");
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

Filter::~Filter() {
  if (read_callbacks_ != nullptr) {
    read_callbacks_->connection().removeConnectionCallbacks(*this);
  }
  if (state_) {
    state_->fail("hbone forwarding filter destroyed before CONNECT readiness");
  }
}

void Filter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  state_ = std::dynamic_pointer_cast<ConnectState>(
      callbacks.connection().streamInfo().filterState()->getDataSharedMutableGeneric(
          ConnectState::Key));
  TcpProxy::Filter::initializeReadFilterCallbacks(callbacks);
  // No upstream pool has been created yet. Intercept its close notifications
  // before TCP proxy starts flushing the downstream connection.
  upstream_callbacks_ = std::make_shared<ConnectCallbacks>(this, state_);
  callbacks.connection().addConnectionCallbacks(*this);
}

void Filter::ConnectCallbacks::onEvent(Network::ConnectionEvent event) {
  // The newer TCP proxy pool reports pre-CONNECT transport failures through
  // UpstreamCallbacks instead of onGenericPoolFailure. Its upstream StreamInfo
  // still carries the reason, including an outer-HBONE rejection.
  if (state_ && state_->phase() == ConnectState::Phase::Pending &&
      (event == Network::ConnectionEvent::LocalClose ||
       event == Network::ConnectionEvent::RemoteClose)) {
    auto& info = parent_->getStreamInfo();
    if (info.upstreamInfo() &&
        permanentConnectRejection(info.upstreamInfo()->upstreamTransportFailureReason())) {
      static_cast<Filter*>(parent_)->rejectConnect(
          info.upstreamInfo()->upstreamTransportFailureReason(),
          info.upstreamInfo()->upstreamHost());
      return;
    }
  }
  if (state_ && state_->phase() == ConnectState::Phase::Accepted &&
      (event == Network::ConnectionEvent::LocalClose ||
       event == Network::ConnectionEvent::RemoteClose)) {
    state_->fail("hbone upstream closed before CONNECT readiness");
  }
  // Pending attempts still belong to TCP proxy's retry policy; established
  // streams retain normal forwarding and drain semantics.
  TcpProxy::Filter::UpstreamCallbacks::onEvent(event);
}

Network::FilterStatus Filter::onNewConnection() {
  if (!state_) {
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush,
                                        "hbone adapter transport socket is missing");
    return Network::FilterStatus::StopIteration;
  }
  const auto* disable_tunneling =
      read_callbacks_->connection()
          .streamInfo()
          .filterState()
          ->getDataReadOnly<StreamInfo::BoolAccessor>(TcpProxy::DisableTunnelingFilterStateKey);
  if (disable_tunneling != nullptr && disable_tunneling->value()) {
    state_->fail("hbone adapter cannot disable CONNECT tunneling");
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush,
                                        "hbone adapter cannot disable CONNECT tunneling");
    return Network::FilterStatus::StopIteration;
  }
  return TcpProxy::Filter::onNewConnection();
}

void Filter::onGenericPoolReady(StreamInfo::StreamInfo* info,
                                std::unique_ptr<TcpProxy::GenericUpstream>&& upstream,
                                Upstream::HostDescriptionConstSharedPtr& host,
                                const Network::ConnectionInfoProvider& address_provider,
                                Ssl::ConnectionInfoConstSharedPtr ssl_info) {
  const auto state = state_;
  TcpProxy::Filter::onGenericPoolReady(info, std::move(upstream), host, address_provider, ssl_info);
  // A close during the base callback wins over acceptance. The socket delivers
  // readiness on a later dispatcher iteration, never inside the HTTP decoder.
  state->accept();
}

void Filter::onGenericPoolFailure(ConnectionPool::PoolFailureReason reason,
                                  absl::string_view failure_reason,
                                  Upstream::HostDescriptionConstSharedPtr host) {
  if (const auto status = rejectedStatus(failure_reason, "tunnel_response:")) {
    rejectConnect(absl::StrCat(RejectionPrefix, *status), std::move(host));
    return;
  }
  if (permanentConnectRejection(failure_reason)) {
    rejectConnect(failure_reason, std::move(host));
    return;
  }
  failure_reason_ = std::string(failure_reason);
  TcpProxy::Filter::onGenericPoolFailure(reason, failure_reason, std::move(host));
}

void Filter::rejectConnect(absl::string_view reason, Upstream::HostDescriptionConstSharedPtr host) {
  // Copy before deleting the pool: reason may refer to its upstream StreamInfo.
  failure_reason_ = std::string(reason);
  state_->fail(failure_reason_);
  connecting_ = false;
  read_callbacks_->upstreamHost(host);
  getStreamInfo().upstreamInfo()->setUpstreamHost(host);
  getStreamInfo().upstreamInfo()->setUpstreamTransportFailureReason(failure_reason_);
  getStreamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamConnectionFailure);
  getStreamInfo().setResponseCodeDetails(failure_reason_);
  // A CONNECT decoder may still be on the stack. Do not run the base failure
  // callback: it reports an outlier failure and schedules TCP proxy retries.
  read_callbacks_->connection().dispatcher().deferredDelete(std::move(generic_conn_pool_));
  TcpProxy::Filter::onInitFailure(UpstreamFailureReason::ConnectFailed);
}

void Filter::onInitFailure(UpstreamFailureReason reason) {
  if (state_) {
    state_->fail(failure_reason_.empty() ? "hbone CONNECT establishment failed" : failure_reason_);
  }
  TcpProxy::Filter::onInitFailure(reason);
}

void Filter::onEvent(Network::ConnectionEvent event) {
  if (state_ && (event == Network::ConnectionEvent::LocalClose ||
                 event == Network::ConnectionEvent::RemoteClose)) {
    state_->fail("hbone internal peer closed before CONNECT readiness");
  }
}

} // namespace Hbone
} // namespace Istio
} // namespace Extensions
} // namespace Envoy
