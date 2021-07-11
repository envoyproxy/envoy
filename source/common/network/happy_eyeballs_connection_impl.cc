#include "source/common/network/happy_eyeballs_connection_impl.h"

#include <vector>

namespace Envoy {
namespace Network {

HappyEyeballsConnectionImpl::HappyEyeballsConnectionImpl(
    Event::Dispatcher& dispatcher,
    const std::vector<Network::Address::InstanceConstSharedPtr>& address_list,
    Network::Address::InstanceConstSharedPtr source_address,
    Network::TransportSocketFactory& socket_factory,
    Network::TransportSocketOptionsSharedPtr transport_socket_options,
    const Network::ConnectionSocket::OptionsSharedPtr options)
    : dispatcher_(dispatcher),
      address_list_(address_list),
      source_address_(source_address),
      socket_factory_(socket_factory),
      transport_socket_options_(transport_socket_options),
      options_(options),
      next_attempt_timer_(dispatcher_.createTimer([this]() -> void { tryAnotherConnection(); })) {
  connections_.push_back(createNextConnection());
}

HappyEyeballsConnectionImpl::~HappyEyeballsConnectionImpl() = default;

void HappyEyeballsConnectionImpl::connect() {
  ASSERT(!connect_finished_);
  connections_[0]->connect();
  maybeScheduleNextAttempt();
}

void HappyEyeballsConnectionImpl::addWriteFilter(WriteFilterSharedPtr filter) {
  ASSERT(connect_finished_);
  connections_[0]->addWriteFilter(filter);
}
void HappyEyeballsConnectionImpl::addFilter(FilterSharedPtr filter) {
  ASSERT(connect_finished_);
  connections_[0]->addFilter(filter);
}
void HappyEyeballsConnectionImpl::addReadFilter(ReadFilterSharedPtr filter) {
  if (connect_finished_) {
    connections_[0]->addReadFilter(filter);
    return;
  }
  post_connect_state_.read_filters_.push_back(filter);
}
void HappyEyeballsConnectionImpl::removeReadFilter(ReadFilterSharedPtr filter) {
  ASSERT(connect_finished_);
  connections_[0]->removeReadFilter(filter);
}
bool HappyEyeballsConnectionImpl::initializeReadFilters() {
  ASSERT(connect_finished_);
  return connections_[0]->initializeReadFilters();
}

void HappyEyeballsConnectionImpl::addBytesSentCallback(ClientConnection::BytesSentCb cb) {
  ASSERT(connect_finished_);
  connections_[0]->addBytesSentCallback(cb);
}
void HappyEyeballsConnectionImpl::enableHalfClose(bool enabled) {
  ASSERT(connect_finished_);
  connections_[0]->enableHalfClose(enabled);
}
bool HappyEyeballsConnectionImpl::isHalfCloseEnabled() {
  ASSERT(connect_finished_);
  return connections_[0]->isHalfCloseEnabled();
}
std::string HappyEyeballsConnectionImpl::nextProtocol() const {
  ASSERT(connect_finished_);
  return connections_[0]->nextProtocol();
}
void HappyEyeballsConnectionImpl::noDelay(bool enable) {
  if (!connect_finished_) {
    per_connection_state_.no_delay_ = enable;
  }
  for (auto& connection : connections_) {
    connection->noDelay(enable);
  }
}
void HappyEyeballsConnectionImpl::readDisable(bool disable) {
  ASSERT(connect_finished_);
  connections_[0]->readDisable(disable);
}
void HappyEyeballsConnectionImpl::detectEarlyCloseWhenReadDisabled(bool value) {
  if (!connect_finished_) {
    per_connection_state_.detect_early_close_when_read_disabled_ = value;
  }
  for (auto& connection : connections_) {
    connection->detectEarlyCloseWhenReadDisabled(value);
  }
}
bool HappyEyeballsConnectionImpl::readEnabled() const {
  if (!connect_finished_) {
    ASSERT(connections_[0]->readEnabled());
    return true;
  }
  return connections_[0]->readEnabled();
}
const SocketAddressProvider& HappyEyeballsConnectionImpl::addressProvider() const {
  ASSERT(connect_finished_);
  return connections_[0]->addressProvider();
}
SocketAddressProviderSharedPtr HappyEyeballsConnectionImpl::addressProviderSharedPtr() const {
  ASSERT(connect_finished_);
  return connections_[0]->addressProviderSharedPtr();
}
absl::optional<ClientConnection::UnixDomainSocketPeerCredentials> HappyEyeballsConnectionImpl::unixSocketPeerCredentials() const {
  ASSERT(connect_finished_);
  return connections_[0]->unixSocketPeerCredentials();
}
Ssl::ConnectionInfoConstSharedPtr HappyEyeballsConnectionImpl::ssl() const {
  ASSERT(connect_finished_);
  return connections_[0]->ssl();
}
Connection::State HappyEyeballsConnectionImpl::state() const {
  if (!connect_finished_) {
    ASSERT(connections_[0]->state() == Connection::State::Open);
  }
  return connections_[0]->state();
}
bool HappyEyeballsConnectionImpl::connecting() const {
  ASSERT(connect_finished_ || connections_[0]->connecting());
  return connections_[0]->connecting();
}
void HappyEyeballsConnectionImpl::write(Buffer::Instance& data, bool end_stream) {
  std::cerr << __FUNCTION__ << ":" << __LINE__ << std::endl;
  if (connect_finished_) {
    std::cerr << __FUNCTION__ << ":" << __LINE__ << std::endl;
    connections_[0]->write(data, end_stream);
    std::cerr << __FUNCTION__ << ":" << __LINE__ << std::endl;
    return;
  }

  std::cerr << __FUNCTION__ << ":" << __LINE__ << std::endl;
  post_connect_state_.write_buffer_ = dispatcher_.getWatermarkFactory().create(
      [this]() -> void { this->onWriteBufferLowWatermark(); },
      [this]() -> void { this->onWriteBufferHighWatermark(); },
      []() -> void { /* TODO(adisuissa): Handle overflow watermark */ });
  post_connect_state_.write_buffer_.value()->move(data);
  std::cerr << __FUNCTION__ << ":" << __LINE__ << std::endl;
  post_connect_state_.end_stream_ = end_stream;
  std::cerr << __FUNCTION__ << ":" << __LINE__ << std::endl;
}
void HappyEyeballsConnectionImpl::setBufferLimits(uint32_t limit) {
  ASSERT(connect_finished_);
  connections_[0]->setBufferLimits(limit);
}
uint32_t HappyEyeballsConnectionImpl::bufferLimit() const {
  // TODO - huh?
  //ASSERT(connect_finished_);
  return connections_[0]->bufferLimit();
}
bool HappyEyeballsConnectionImpl::aboveHighWatermark() const {
  /*
    if (!connect_finished_) {
    return false;
    }
  */
  return connections_[0]->aboveHighWatermark();
}
const ConnectionSocket::OptionsSharedPtr& HappyEyeballsConnectionImpl::socketOptions() const {
  ASSERT(connect_finished_);
  return connections_[0]->socketOptions();
}
absl::string_view HappyEyeballsConnectionImpl::requestedServerName() const {
  ASSERT(connect_finished_);
  return connections_[0]->requestedServerName();
}
StreamInfo::StreamInfo& HappyEyeballsConnectionImpl::streamInfo() {
  ASSERT(connect_finished_);
  return connections_[0]->streamInfo();
}
const StreamInfo::StreamInfo& HappyEyeballsConnectionImpl::streamInfo() const {
  ASSERT(connect_finished_);
  return connections_[0]->streamInfo();
}
absl::string_view HappyEyeballsConnectionImpl::transportFailureReason() const {
  ASSERT(connect_finished_);
  return connections_[0]->transportFailureReason();
}
bool HappyEyeballsConnectionImpl::startSecureTransport() {
  ASSERT(connect_finished_);
  return connections_[0]->startSecureTransport();
}
absl::optional<std::chrono::milliseconds> HappyEyeballsConnectionImpl::lastRoundTripTime() const {
  ASSERT(connect_finished_);
  return connections_[0]->lastRoundTripTime();
}
void HappyEyeballsConnectionImpl::addConnectionCallbacks(ConnectionCallbacks& cb) {
  if (connect_finished_) {
    connections_[0]->addConnectionCallbacks(cb);
    return;
  }
  post_connect_state_.connection_callbacks_.push_back(&cb);
}
void HappyEyeballsConnectionImpl::removeConnectionCallbacks(ConnectionCallbacks& cb) {
  if (connect_finished_) {
    connections_[0]->removeConnectionCallbacks(cb);
    return;
  }
  auto i = post_connect_state_.connection_callbacks_.begin();
  while (i != post_connect_state_.connection_callbacks_.end()) {
    if (*i == &cb) {
      post_connect_state_.connection_callbacks_.erase(i);
      return;
    }
  }
  ASSERT(false);
}
void HappyEyeballsConnectionImpl::close(ConnectionCloseType type) {
  // TODO(XXX)
  //ASSERT(connect_finished_);
  connections_[0]->close(type);
}
Event::Dispatcher& HappyEyeballsConnectionImpl::dispatcher() {
  //ASSERT(connect_finished_);
  return connections_[0]->dispatcher();
}
uint64_t HappyEyeballsConnectionImpl::id() const {
  ASSERT(connect_finished_);
  return connections_[0]->id();
}
void HappyEyeballsConnectionImpl::hashKey(std::vector<uint8_t>& hash) const {
  ASSERT(connect_finished_);
  connections_[0]->hashKey(hash);
}
void HappyEyeballsConnectionImpl::setConnectionStats(const ConnectionStats& stats) {
  //  ASSERT(connect_finished_);
  // TODO more
  connections_[0]->setConnectionStats(stats);
}
void HappyEyeballsConnectionImpl::setDelayedCloseTimeout(std::chrono::milliseconds timeout) {
  ASSERT(connect_finished_);
  connections_[0]->setDelayedCloseTimeout(timeout);
}

// ScopeTrackedObject
void HappyEyeballsConnectionImpl::dumpState(std::ostream& os, int indent_level) const {
  ASSERT(connect_finished_);
  connections_[0]->dumpState(os, indent_level);
}


std::unique_ptr<ClientConnection> HappyEyeballsConnectionImpl::createNextConnection() {
  ASSERT(next_address_ < address_list_.size());
  std::cerr << __FUNCTION__ << "\n";
  auto connection = dispatcher_.createClientConnection(
      address_list_[next_address_++], source_address_,
      socket_factory_.createTransportSocket(transport_socket_options_),
      options_);
  std::cerr << "connection: " << connection.get() << std::endl;
  callbacks_wrappers_.push_back(std::make_unique<ConnectionCallbacksWrapper>(*this, *connection));
  std::cerr << "connection: " << connection.get() << std::endl;
  connection->addConnectionCallbacks(*callbacks_wrappers_.back());
  std::cerr << "connection: " << connection.get() << std::endl;

  if (per_connection_state_.detect_early_close_when_read_disabled_.has_value()) {
    connection->detectEarlyCloseWhenReadDisabled(per_connection_state_.detect_early_close_when_read_disabled_.value());
  }
  if (per_connection_state_.no_delay_.has_value()) {
    connection->noDelay(per_connection_state_.no_delay_.value());
  }
  return connection;
}

void HappyEyeballsConnectionImpl::tryAnotherConnection() {
  std::cerr << __FUNCTION__ << "\n";
  connections_.push_back(createNextConnection());
  connections_.back()->connect();
  maybeScheduleNextAttempt();
}

void HappyEyeballsConnectionImpl::maybeScheduleNextAttempt() {
  std::cerr << __FUNCTION__ << "\n";
  if (next_address_ >= address_list_.size()) {
    return;
  }
  std::cerr << __FUNCTION__ << "\n";
  next_attempt_timer_->enableTimer(std::chrono::milliseconds(300));
}

void HappyEyeballsConnectionImpl::onEvent(ConnectionEvent event, ConnectionCallbacksWrapper* wrapper) {
  std::cerr << __FUNCTION__ << " " << static_cast<int>(event) << " 1\n";
  //ASSERT(wrapper == callbacks_wrapper_.get());

  wrapper->connection().removeConnectionCallbacks(*wrapper);
  if (event != ConnectionEvent::Connected) {
    if (next_address_ < address_list_.size()) {
      next_attempt_timer_->disableTimer();
      tryAnotherConnection();
    }
    if (connections_.size() > 1) {
      // Nuke this connection and associated callbacks and let a subsequent attempt proceed.
      cleanupWrapperAndConnection(wrapper);
      return;
    }
    //ASSERT(false); /// XXX have coverage for this case.
  }

  connect_finished_ = true;

  // Clean up other connections.
  std::cerr << __FUNCTION__ << " " << connections_.size() << " \n";
  std::cerr << __FUNCTION__ << " " << callbacks_wrappers_.size() << " \n";

  {
  auto it = connections_.begin();
  while (it != connections_.end()) {
    if (it->get() != &(wrapper->connection())) {
      (*it)->close(ConnectionCloseType::NoFlush);
      it = connections_.erase(it);
    } else {
      ++it;
    }
  }
  }
  {
  auto it = callbacks_wrappers_.begin();
  while (it != callbacks_wrappers_.end()) {
    if (it->get() != wrapper) {
      it = callbacks_wrappers_.erase(it);
    } else {
      ++it;
    }
  }
  }
  /*
  std::remove_if(connections_.begin(),  connections_.end(), [wrapper](std::unique_ptr<ClientConnection>& connection) { return connection.get() != &(wrapper->connection()); });
  std::remove_if(callbacks_wrappers_.begin(), callbacks_wrappers_.end(), [wrapper](std::unique_ptr<ConnectionCallbacksWrapper>& callbacks_wrapper) { return callbacks_wrapper.get() != wrapper; });
  */
  std::cerr << __FUNCTION__ << " " << connections_.size() << " \n";
  std::cerr << __FUNCTION__ << " " << callbacks_wrappers_.size() << " \n";
  ASSERT(connections_.size() == 1);
  ASSERT(callbacks_wrappers_.size() == 1);

  callbacks_wrappers_.clear();
  // Apply post-connect state to the final socket.
  for (auto cb : post_connect_state_.connection_callbacks_) {
    if (cb) {
      connections_[0]->addConnectionCallbacks(*cb);
    }
  }
  if (event == ConnectionEvent::Connected) {
    for (auto filter : post_connect_state_.read_filters_) {
      connections_[0]->addReadFilter(filter);
    }
  }

  if (post_connect_state_.write_buffer_.has_value()) {
    //ASSERT(false);
    // write_buffer_ and end_stream_ are both set together in write().
    ASSERT(post_connect_state_.end_stream_.has_value());
    connections_[0]->write(*post_connect_state_.write_buffer_.value(), post_connect_state_.end_stream_.value());
  }

  std::vector<ConnectionCallbacks*> cbs;
  cbs.swap(post_connect_state_.connection_callbacks_);
  /*
    for (auto cb : cbs) {
    std::cerr << __FUNCTION__ << " calling cb->onEvent\n";
    cb->onEvent(event);
    std::cerr << __FUNCTION__ << " done\n";
    }
  */
  std::cerr << __FUNCTION__ << " finished\n";
}

void HappyEyeballsConnectionImpl::cleanupWrapperAndConnection(ConnectionCallbacksWrapper* wrapper) {
  std::cerr << __FUNCTION__ << " " << connections_.size() << " \n";
  std::cerr << __FUNCTION__ << " " << callbacks_wrappers_.size() << " \n";

  {
  auto it = connections_.begin();
  while (it != connections_.end()) {
    if (it->get() == &(wrapper->connection())) {
      (*it)->close(ConnectionCloseType::NoFlush);
      it = connections_.erase(it);
    } else {
      ++it;
    }
  }
  }
  {
  auto it = callbacks_wrappers_.begin();
  while (it != callbacks_wrappers_.end()) {
    if (it->get() == wrapper) {
      it = callbacks_wrappers_.erase(it);
    } else {
      ++it;
    }
  }
  }

  std::cerr << __FUNCTION__ << " " << connections_.size() << " \n";
  std::cerr << __FUNCTION__ << " " << callbacks_wrappers_.size() << " \n";
}

void HappyEyeballsConnectionImpl::onAboveWriteBufferHighWatermark(ConnectionCallbacksWrapper* /*wrapper*/) {
  ASSERT(connect_finished_);
  //ASSERT(wrapper == callbacks_wrapper_.get());
}

void HappyEyeballsConnectionImpl::onBelowWriteBufferLowWatermark(ConnectionCallbacksWrapper* /*wrapper*/) {
  ASSERT(connect_finished_);
  //ASSERT(wrapper == callbacks_wrapper_.get());
}

void HappyEyeballsConnectionImpl::onWriteBufferHighWatermark() {
  ASSERT(false);
}
void HappyEyeballsConnectionImpl::onWriteBufferLowWatermark() {
  ASSERT(false);
}

} // namespace Network
} // namespace Envoy
