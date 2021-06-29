#include "source/common/network/happy_eyeballs_connection_impl.h"

namespace Envoy {
namespace Network {

HappyEyeballsConnectionImpl::HappyEyeballsConnectionImpl(
    Event::Dispatcher& dispatcher,
    Network::Address::InstanceConstSharedPtr address,
    Network::Address::InstanceConstSharedPtr source_address,
    Network::TransportSocketFactory& socket_factory,
    Network::TransportSocketOptionsSharedPtr transport_socket_options,
    const Network::ConnectionSocket::OptionsSharedPtr options)
    : dispatcher_(dispatcher),
      address_(address),
      source_address_(source_address),
      socket_factory_(socket_factory),
      transport_socket_options_(transport_socket_options),
      options_(options) {
  connection_ = createConnection();
  state_.write_buffer_ = dispatcher_.getWatermarkFactory().create(
          [this]() -> void { this->onWriteBufferLowWatermark(); },
          [this]() -> void { this->onWriteBufferHighWatermark(); },
          []() -> void { /* TODO(adisuissa): Handle overflow watermark */ });
}

HappyEyeballsConnectionImpl::~HappyEyeballsConnectionImpl() = default;

void HappyEyeballsConnectionImpl::connect() {
  ASSERT(!connect_finished_);
  connection_->connect();
}

void HappyEyeballsConnectionImpl::addWriteFilter(WriteFilterSharedPtr filter) {
  ASSERT(connect_finished_);
  connection_->addWriteFilter(filter);
}
void HappyEyeballsConnectionImpl::addFilter(FilterSharedPtr filter) {
  ASSERT(connect_finished_);
  connection_->addFilter(filter);
}
void HappyEyeballsConnectionImpl::addReadFilter(ReadFilterSharedPtr filter) {
  if (connect_finished_) {
    connection_->addReadFilter(filter);
    return;
  }
  read_filters_.push_back(filter);
}
void HappyEyeballsConnectionImpl::removeReadFilter(ReadFilterSharedPtr filter) {
  ASSERT(connect_finished_);
  connection_->removeReadFilter(filter);
}
bool HappyEyeballsConnectionImpl::initializeReadFilters() {
  ASSERT(connect_finished_);
  return connection_->initializeReadFilters();
}

void HappyEyeballsConnectionImpl::addBytesSentCallback(ClientConnection::BytesSentCb cb) {
  ASSERT(connect_finished_);
  connection_->addBytesSentCallback(cb);
}
void HappyEyeballsConnectionImpl::enableHalfClose(bool enabled) {
  ASSERT(connect_finished_);
  connection_->enableHalfClose(enabled);
}
bool HappyEyeballsConnectionImpl::isHalfCloseEnabled() {
  ASSERT(connect_finished_);
  return connection_->isHalfCloseEnabled();
}
std::string HappyEyeballsConnectionImpl::nextProtocol() const {
  ASSERT(connect_finished_);
  return connection_->nextProtocol();
}
void HappyEyeballsConnectionImpl::noDelay(bool enable) {
  if (!connect_finished_) {
    state_.noDelay_ = enable;
  }
  connection_->noDelay(enable);
}
void HappyEyeballsConnectionImpl::readDisable(bool disable) {
  ASSERT(connect_finished_);
  connection_->readDisable(disable);
}
void HappyEyeballsConnectionImpl::detectEarlyCloseWhenReadDisabled(bool value) {
  if (!connect_finished_) {
    state_.detectEarlyCloseWhenReadDisabled_ = value;
  }
  connection_->detectEarlyCloseWhenReadDisabled(value);
}
bool HappyEyeballsConnectionImpl::readEnabled() const {
  if (!connect_finished_) {
    return true;
  }
  return connection_->readEnabled();
}
const SocketAddressProvider& HappyEyeballsConnectionImpl::addressProvider() const {
  ASSERT(connect_finished_);
  return connection_->addressProvider();
}
SocketAddressProviderSharedPtr HappyEyeballsConnectionImpl::addressProviderSharedPtr() const {
  ASSERT(connect_finished_);
  return connection_->addressProviderSharedPtr();
}
absl::optional<ClientConnection::UnixDomainSocketPeerCredentials> HappyEyeballsConnectionImpl::unixSocketPeerCredentials() const {
  ASSERT(connect_finished_);
  return connection_->unixSocketPeerCredentials();
}
Ssl::ConnectionInfoConstSharedPtr HappyEyeballsConnectionImpl::ssl() const {
  ASSERT(connect_finished_);
  return connection_->ssl();
}
Connection::State HappyEyeballsConnectionImpl::state() const {
  //ASSERT(
  if (!connect_finished_) {
    ASSERT(connection_->state() == Connection::State::Open);
  }
  return connection_->state();
}
bool HappyEyeballsConnectionImpl::connecting() const {
  ASSERT(connect_finished_ || connection_->connecting());
  return connection_->connecting();
}
void HappyEyeballsConnectionImpl::write(Buffer::Instance& data, bool end_stream) {
    std::cerr << __FUNCTION__ << ":" << __LINE__ << std::endl;
  if (connect_finished_) {
    std::cerr << __FUNCTION__ << ":" << __LINE__ << std::endl;
    connection_->write(data, end_stream);
    std::cerr << __FUNCTION__ << ":" << __LINE__ << std::endl;
    return;
  }

  std::cerr << __FUNCTION__ << ":" << __LINE__ << std::endl;
  state_.write_buffer_->move(data);
  std::cerr << __FUNCTION__ << ":" << __LINE__ << std::endl;
  state_.end_stream_ = end_stream;
  std::cerr << __FUNCTION__ << ":" << __LINE__ << std::endl;
}
void HappyEyeballsConnectionImpl::setBufferLimits(uint32_t limit) {
  ASSERT(connect_finished_);
  connection_->setBufferLimits(limit);
}
uint32_t HappyEyeballsConnectionImpl::bufferLimit() const {
  // TODO - huh?
  //ASSERT(connect_finished_);
  return connection_->bufferLimit();
}
bool HappyEyeballsConnectionImpl::aboveHighWatermark() const {
  if (!connect_finished_) {
    return false;
  }

  return connection_->aboveHighWatermark();
}
const ConnectionSocket::OptionsSharedPtr& HappyEyeballsConnectionImpl::socketOptions() const {
  ASSERT(connect_finished_);
  return connection_->socketOptions();
}
absl::string_view HappyEyeballsConnectionImpl::requestedServerName() const {
  ASSERT(connect_finished_);
  return connection_->requestedServerName();
}
StreamInfo::StreamInfo& HappyEyeballsConnectionImpl::streamInfo() {
  ASSERT(connect_finished_);
  return connection_->streamInfo();
}
const StreamInfo::StreamInfo& HappyEyeballsConnectionImpl::streamInfo() const {
  ASSERT(connect_finished_);
  return connection_->streamInfo();
}
absl::string_view HappyEyeballsConnectionImpl::transportFailureReason() const {
  ASSERT(connect_finished_);
  return connection_->transportFailureReason();
}
bool HappyEyeballsConnectionImpl::startSecureTransport() {
  ASSERT(connect_finished_);
  return connection_->startSecureTransport();
}
absl::optional<std::chrono::milliseconds> HappyEyeballsConnectionImpl::lastRoundTripTime() const {
  ASSERT(connect_finished_);
  return connection_->lastRoundTripTime();
}
void HappyEyeballsConnectionImpl::addConnectionCallbacks(ConnectionCallbacks& cb) {
  if (connect_finished_) {
    connection_->addConnectionCallbacks(cb);
    return;
  }
  cbs_.push_back(&cb);
}
void HappyEyeballsConnectionImpl::removeConnectionCallbacks(ConnectionCallbacks& cb) {
  if (connect_finished_) {
    connection_->removeConnectionCallbacks(cb);
    return;
  }
  auto i = cbs_.begin();
  while (i != cbs_.end()) {
    if (*i == &cb) {
      cbs_.erase(i);
      return;
    }
  }
  ASSERT(false);
}
void HappyEyeballsConnectionImpl::close(ConnectionCloseType type) {
  // TODO(XXX)
  //ASSERT(connect_finished_);
  connection_->close(type);
}
Event::Dispatcher& HappyEyeballsConnectionImpl::dispatcher() {
  //ASSERT(connect_finished_);
  return connection_->dispatcher();
}
uint64_t HappyEyeballsConnectionImpl::id() const {
  ASSERT(connect_finished_);
  return connection_->id();
}
void HappyEyeballsConnectionImpl::hashKey(std::vector<uint8_t>& hash) const {
  ASSERT(connect_finished_);
  connection_->hashKey(hash);
}
void HappyEyeballsConnectionImpl::setConnectionStats(const ConnectionStats& stats) {
  //  ASSERT(connect_finished_);
  // TODO more
  connection_->setConnectionStats(stats);
}
void HappyEyeballsConnectionImpl::setDelayedCloseTimeout(std::chrono::milliseconds timeout) {
  ASSERT(connect_finished_);
  connection_->setDelayedCloseTimeout(timeout);
}

// ScopeTrackedObject
void HappyEyeballsConnectionImpl::dumpState(std::ostream& os, int indent_level) const {
  connection_->dumpState(os, indent_level);
}


std::unique_ptr<ClientConnection> HappyEyeballsConnectionImpl::createConnection() {
  auto connection = dispatcher_.createClientConnection(
      address_, source_address_,
      socket_factory_.createTransportSocket(transport_socket_options_),
      options_);
  callbacks_wrapper_ = std::make_unique<ConnectionCallbacksWrapper>(*this, *connection);
  connection->addConnectionCallbacks(*callbacks_wrapper_);
  return connection;
}

void HappyEyeballsConnectionImpl::onEvent(ConnectionEvent event, ConnectionCallbacksWrapper* wrapper) {
  std::cerr << __FUNCTION__ << " " << static_cast<int>(event) << " 1\n";
  ASSERT(wrapper == callbacks_wrapper_.get());
  connect_finished_ = true;

  connection_->removeConnectionCallbacks(*callbacks_wrapper_);
  callbacks_wrapper_ = nullptr;
  for (auto cb : cbs_) {
    if (cb) {
      connection_->addConnectionCallbacks(*cb);
    }
  }
  if (event == ConnectionEvent::Connected) {
    for (auto filter : read_filters_) {
      connection_->addReadFilter(filter);
    }
  }

  if (state_.write_buffer_->length() > 0 || state_.end_stream_) {
    connection_->write(*state_.write_buffer_, state_.end_stream_);
  }

  std::vector<ConnectionCallbacks*> cbs;
  cbs.swap(cbs_);
  /*
  for (auto cb : cbs) {
    std::cerr << __FUNCTION__ << " calling cb->onEvent\n";
    cb->onEvent(event);
    std::cerr << __FUNCTION__ << " done\n";
  }
  */
  std::cerr << __FUNCTION__ << " finished\n";
}

void HappyEyeballsConnectionImpl::onAboveWriteBufferHighWatermark(ConnectionCallbacksWrapper* wrapper) {
  ASSERT(connect_finished_);
  ASSERT(wrapper == callbacks_wrapper_.get());
}

void HappyEyeballsConnectionImpl::onBelowWriteBufferLowWatermark(ConnectionCallbacksWrapper* wrapper) {
  ASSERT(connect_finished_);
  ASSERT(wrapper == callbacks_wrapper_.get());
}

void HappyEyeballsConnectionImpl::onWriteBufferHighWatermark() {
  ASSERT(false);
}
void HappyEyeballsConnectionImpl::onWriteBufferLowWatermark() {
  ASSERT(false);
}

} // namespace Network
} // namespace Envoy
