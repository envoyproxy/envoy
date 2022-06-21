#include "source/common/network/multi_connection_base_impl.h"

#include <vector>

namespace Envoy {
namespace Network {

MultiConnectionBaseImpl::MultiConnectionBaseImpl(Event::Dispatcher& dispatcher,
                                                 ConnectionProviderPtr connection_provider)
    : id_(ConnectionImpl::next_global_id_++), dispatcher_(dispatcher),
      connection_provider_(std::move(connection_provider)),
      next_attempt_timer_(dispatcher_.createTimer([this]() -> void { tryAnotherConnection(); })) {
  ENVOY_LOG_EVENT(debug, "multi_connection_new_cx", "[C{}] connections={}", id_,
                  connection_provider_->totalConnections());
  connections_.push_back(createNextConnection());
}

MultiConnectionBaseImpl::~MultiConnectionBaseImpl() = default;

void MultiConnectionBaseImpl::connect() {
  ENVOY_BUG(!connect_finished_, "connection already connected");
  connections_[0]->connect();
  maybeScheduleNextAttempt();
}

void MultiConnectionBaseImpl::addWriteFilter(WriteFilterSharedPtr filter) {
  if (connect_finished_) {
    connections_[0]->addWriteFilter(filter);
    return;
  }
  // Filters should only be notified of events on the final connection, so defer adding
  // filters until the final connection has been determined.
  post_connect_state_.write_filters_.push_back(filter);
}

void MultiConnectionBaseImpl::addFilter(FilterSharedPtr filter) {
  if (connect_finished_) {
    connections_[0]->addFilter(filter);
    return;
  }
  // Filters should only be notified of events on the final connection, so defer adding
  // filters until the final connection has been determined.
  post_connect_state_.filters_.push_back(filter);
}

void MultiConnectionBaseImpl::addReadFilter(ReadFilterSharedPtr filter) {
  if (connect_finished_) {
    connections_[0]->addReadFilter(filter);
    return;
  }
  // Filters should only be notified of events on the final connection, so defer adding
  // filters until the final connection has been determined.
  post_connect_state_.read_filters_.push_back(filter);
}

void MultiConnectionBaseImpl::removeReadFilter(ReadFilterSharedPtr filter) {
  if (connect_finished_) {
    connections_[0]->removeReadFilter(filter);
    return;
  }
  // Filters should only be notified of events on the final connection, so remove
  // the filters from the list of deferred filters.
  auto i = post_connect_state_.read_filters_.begin();
  while (i != post_connect_state_.read_filters_.end()) {
    if (*i == filter) {
      post_connect_state_.read_filters_.erase(i);
      return;
    }
  }
  IS_ENVOY_BUG("Failed to remove read filter");
}

bool MultiConnectionBaseImpl::initializeReadFilters() {
  if (connect_finished_) {
    return connections_[0]->initializeReadFilters();
  }
  // Filters should only be notified of events on the final connection, so defer
  // initialization of the filters until the final connection has been determined.
  if (post_connect_state_.read_filters_.empty()) {
    return false;
  }
  post_connect_state_.initialize_read_filters_ = true;
  return true;
}

void MultiConnectionBaseImpl::addBytesSentCallback(Connection::BytesSentCb cb) {
  if (connect_finished_) {
    connections_[0]->addBytesSentCallback(cb);
    return;
  }
  // Callbacks should only be notified of events on the final connection, so defer adding
  // callbacks until the final connection has been determined.
  post_connect_state_.bytes_sent_callbacks_.push_back(cb);
}

void MultiConnectionBaseImpl::enableHalfClose(bool enabled) {
  if (!connect_finished_) {
    per_connection_state_.enable_half_close_ = enabled;
  }
  for (auto& connection : connections_) {
    connection->enableHalfClose(enabled);
  }
}

bool MultiConnectionBaseImpl::isHalfCloseEnabled() { return connections_[0]->isHalfCloseEnabled(); }

std::string MultiConnectionBaseImpl::nextProtocol() const {
  return connections_[0]->nextProtocol();
}

void MultiConnectionBaseImpl::noDelay(bool enable) {
  if (!connect_finished_) {
    per_connection_state_.no_delay_ = enable;
  }
  for (auto& connection : connections_) {
    connection->noDelay(enable);
  }
}

void MultiConnectionBaseImpl::readDisable(bool disable) {
  if (connect_finished_) {
    connections_[0]->readDisable(disable);
    return;
  }
  if (!post_connect_state_.read_disable_count_.has_value()) {
    post_connect_state_.read_disable_count_ = 0;
  }

  if (disable) {
    post_connect_state_.read_disable_count_.value()++;
  } else {
    ASSERT(post_connect_state_.read_disable_count_ != 0);
    post_connect_state_.read_disable_count_.value()--;
  }
}

void MultiConnectionBaseImpl::detectEarlyCloseWhenReadDisabled(bool value) {
  if (!connect_finished_) {
    per_connection_state_.detect_early_close_when_read_disabled_ = value;
  }
  for (auto& connection : connections_) {
    connection->detectEarlyCloseWhenReadDisabled(value);
  }
}

bool MultiConnectionBaseImpl::readEnabled() const {
  if (!connect_finished_) {
    return !post_connect_state_.read_disable_count_.has_value() ||
           post_connect_state_.read_disable_count_ == 0;
  }
  return connections_[0]->readEnabled();
}

ConnectionInfoSetter& MultiConnectionBaseImpl::connectionInfoSetter() {
  return connections_[0]->connectionInfoSetter();
}

const ConnectionInfoProvider& MultiConnectionBaseImpl::connectionInfoProvider() const {
  return connections_[0]->connectionInfoProvider();
}

ConnectionInfoProviderSharedPtr MultiConnectionBaseImpl::connectionInfoProviderSharedPtr() const {
  return connections_[0]->connectionInfoProviderSharedPtr();
}

absl::optional<Connection::UnixDomainSocketPeerCredentials>
MultiConnectionBaseImpl::unixSocketPeerCredentials() const {
  return connections_[0]->unixSocketPeerCredentials();
}

Ssl::ConnectionInfoConstSharedPtr MultiConnectionBaseImpl::ssl() const {
  return connections_[0]->ssl();
}

Connection::State MultiConnectionBaseImpl::state() const {
  if (!connect_finished_) {
    ASSERT(connections_[0]->state() == Connection::State::Open);
  }
  return connections_[0]->state();
}

bool MultiConnectionBaseImpl::connecting() const {
  ASSERT(connect_finished_ || connections_[0]->connecting());
  return connections_[0]->connecting();
}

void MultiConnectionBaseImpl::write(Buffer::Instance& data, bool end_stream) {
  if (connect_finished_) {
    connections_[0]->write(data, end_stream);
    return;
  }

  // Data should only be written on the final connection, so defer actually writing
  // until the final connection has been determined.
  if (!post_connect_state_.write_buffer_.has_value()) {
    post_connect_state_.end_stream_ = false;
    post_connect_state_.write_buffer_ = dispatcher_.getWatermarkFactory().createBuffer(
        [this]() -> void { this->onWriteBufferLowWatermark(); },
        [this]() -> void { this->onWriteBufferHighWatermark(); },
        // ConnectionCallbacks do not have a method to receive overflow watermark
        // notification. So this class, like ConnectionImpl, has a no-op handler.
        []() -> void { /* TODO(adisuissa): Handle overflow watermark */ });
    if (per_connection_state_.buffer_limits_.has_value()) {
      post_connect_state_.write_buffer_.value()->setWatermarks(
          per_connection_state_.buffer_limits_.value());
    }
  }

  post_connect_state_.write_buffer_.value()->move(data);
  ASSERT(!post_connect_state_.end_stream_.value()); // Don't write after end_stream.
  post_connect_state_.end_stream_ = end_stream;
}

void MultiConnectionBaseImpl::setBufferLimits(uint32_t limit) {
  if (!connect_finished_) {
    ASSERT(!per_connection_state_.buffer_limits_.has_value());
    per_connection_state_.buffer_limits_ = limit;
    if (post_connect_state_.write_buffer_.has_value()) {
      post_connect_state_.write_buffer_.value()->setWatermarks(limit);
    }
  }
  for (auto& connection : connections_) {
    connection->setBufferLimits(limit);
  }
}

uint32_t MultiConnectionBaseImpl::bufferLimit() const { return connections_[0]->bufferLimit(); }

bool MultiConnectionBaseImpl::aboveHighWatermark() const {
  if (!connect_finished_) {
    // Writes are deferred, so return the watermark status from the deferred write buffer.
    return post_connect_state_.write_buffer_.has_value() &&
           post_connect_state_.write_buffer_.value()->highWatermarkTriggered();
  }

  return connections_[0]->aboveHighWatermark();
}

const ConnectionSocket::OptionsSharedPtr& MultiConnectionBaseImpl::socketOptions() const {
  // Note, this might change before connect finishes.
  return connections_[0]->socketOptions();
}

absl::string_view MultiConnectionBaseImpl::requestedServerName() const {
  // Note, this might change before connect finishes.
  return connections_[0]->requestedServerName();
}

StreamInfo::StreamInfo& MultiConnectionBaseImpl::streamInfo() {
  // Note, this might change before connect finishes.
  return connections_[0]->streamInfo();
}

const StreamInfo::StreamInfo& MultiConnectionBaseImpl::streamInfo() const {
  // Note, this might change before connect finishes.
  return connections_[0]->streamInfo();
}

absl::string_view MultiConnectionBaseImpl::transportFailureReason() const {
  // Note, this might change before connect finishes.
  return connections_[0]->transportFailureReason();
}

bool MultiConnectionBaseImpl::startSecureTransport() {
  if (!connect_finished_) {
    per_connection_state_.start_secure_transport_ = true;
  }
  bool ret = true;
  for (auto& connection : connections_) {
    if (!connection->startSecureTransport()) {
      ret = false;
    }
  }
  return ret;
}

absl::optional<std::chrono::milliseconds> MultiConnectionBaseImpl::lastRoundTripTime() const {
  // Note, this might change before connect finishes.
  return connections_[0]->lastRoundTripTime();
}

absl::optional<uint64_t> MultiConnectionBaseImpl::congestionWindowInBytes() const {
  // Note, this value changes constantly even within the same connection.
  return connections_[0]->congestionWindowInBytes();
}

void MultiConnectionBaseImpl::addConnectionCallbacks(ConnectionCallbacks& cb) {
  if (connect_finished_) {
    connections_[0]->addConnectionCallbacks(cb);
    return;
  }
  // Callbacks should only be notified of events on the final connection, so defer adding
  // callbacks until the final connection has been determined.
  post_connect_state_.connection_callbacks_.push_back(&cb);
}

void MultiConnectionBaseImpl::removeConnectionCallbacks(ConnectionCallbacks& cb) {
  if (connect_finished_) {
    connections_[0]->removeConnectionCallbacks(cb);
    return;
  }
  // Callbacks should only be notified of events on the final connection, so remove
  // the callback from the list of deferred callbacks.
  auto i = post_connect_state_.connection_callbacks_.begin();
  while (i != post_connect_state_.connection_callbacks_.end()) {
    if (*i == &cb) {
      post_connect_state_.connection_callbacks_.erase(i);
      return;
    }
  }
  IS_ENVOY_BUG("Failed to remove connection callbacks");
}

void MultiConnectionBaseImpl::close(ConnectionCloseType type) {
  if (connect_finished_) {
    connections_[0]->close(type);
    return;
  }

  connect_finished_ = true;
  ENVOY_LOG(trace, "Disabling next attempt timer.");
  next_attempt_timer_->disableTimer();
  for (size_t i = 0; i < connections_.size(); ++i) {
    connections_[i]->removeConnectionCallbacks(*callbacks_wrappers_[i]);
    if (i != 0) {
      // Wait to close the final connection until the post-connection callbacks
      // have been added.
      connections_[i]->close(ConnectionCloseType::NoFlush);
    }
  }
  connections_.resize(1);
  callbacks_wrappers_.clear();

  for (auto cb : post_connect_state_.connection_callbacks_) {
    if (cb) {
      connections_[0]->addConnectionCallbacks(*cb);
    }
  }
  connections_[0]->close(type);
}

Event::Dispatcher& MultiConnectionBaseImpl::dispatcher() {
  ASSERT(&dispatcher_ == &connections_[0]->dispatcher());
  return connections_[0]->dispatcher();
}

uint64_t MultiConnectionBaseImpl::id() const { return id_; }

void MultiConnectionBaseImpl::hashKey(std::vector<uint8_t>& hash_key) const {
  // Pack the id into sizeof(id_) uint8_t entries in the hash_key vector.
  hash_key.reserve(hash_key.size() + sizeof(id_));
  for (unsigned i = 0; i < sizeof(id_); ++i) {
    hash_key.push_back(0xFF & (id_ >> (8 * i)));
  }
}

void MultiConnectionBaseImpl::setConnectionStats(const ConnectionStats& stats) {
  if (!connect_finished_) {
    per_connection_state_.connection_stats_ = std::make_unique<ConnectionStats>(stats);
  }
  for (auto& connection : connections_) {
    connection->setConnectionStats(stats);
  }
}

void MultiConnectionBaseImpl::setDelayedCloseTimeout(std::chrono::milliseconds timeout) {
  if (!connect_finished_) {
    per_connection_state_.delayed_close_timeout_ = timeout;
  }
  for (auto& connection : connections_) {
    connection->setDelayedCloseTimeout(timeout);
  }
}

void MultiConnectionBaseImpl::dumpState(std::ostream& os, int indent_level) const {
  const char* spaces = spacesForLevel(indent_level);
  os << spaces << "MultiConnectionBaseImpl " << this << DUMP_MEMBER(id_)
     << DUMP_MEMBER(connect_finished_) << "\n";

  for (auto& connection : connections_) {
    DUMP_DETAILS(connection);
  }
}

ClientConnectionPtr MultiConnectionBaseImpl::createNextConnection() {
  ASSERT(connection_provider_->hasNextConnection());
  auto connection = connection_provider_->createNextConnection(id_);
  callbacks_wrappers_.push_back(std::make_unique<ConnectionCallbacksWrapper>(*this, *connection));
  connection->addConnectionCallbacks(*callbacks_wrappers_.back());

  if (per_connection_state_.detect_early_close_when_read_disabled_.has_value()) {
    connection->detectEarlyCloseWhenReadDisabled(
        per_connection_state_.detect_early_close_when_read_disabled_.value());
  }
  if (per_connection_state_.no_delay_.has_value()) {
    connection->noDelay(per_connection_state_.no_delay_.value());
  }
  if (per_connection_state_.connection_stats_) {
    connection->setConnectionStats(*per_connection_state_.connection_stats_);
  }
  if (per_connection_state_.buffer_limits_.has_value()) {
    connection->setBufferLimits(per_connection_state_.buffer_limits_.value());
  }
  if (per_connection_state_.enable_half_close_.has_value()) {
    connection->enableHalfClose(per_connection_state_.enable_half_close_.value());
  }
  if (per_connection_state_.delayed_close_timeout_.has_value()) {
    connection->setDelayedCloseTimeout(per_connection_state_.delayed_close_timeout_.value());
  }
  if (per_connection_state_.start_secure_transport_.has_value()) {
    ASSERT(per_connection_state_.start_secure_transport_);
    connection->startSecureTransport();
  }

  return connection;
}

void MultiConnectionBaseImpl::tryAnotherConnection() {
  ENVOY_LOG(trace, "Trying another connection.");
  connections_.push_back(createNextConnection());
  connections_.back()->connect();
  maybeScheduleNextAttempt();
}

void MultiConnectionBaseImpl::maybeScheduleNextAttempt() {
  if (!connection_provider_->hasNextConnection()) {
    return;
  }
  ENVOY_LOG(trace, "Scheduling next attempt.");
  next_attempt_timer_->enableTimer(std::chrono::milliseconds(300));
}

void MultiConnectionBaseImpl::onEvent(ConnectionEvent event, ConnectionCallbacksWrapper* wrapper) {
  switch (event) {
  case ConnectionEvent::Connected: {
    ENVOY_CONN_LOG_EVENT(debug, "multi_connection_cx_ok", "connection={}", *this,
                         connection_provider_->nextConnection());
    break;
  }
  case ConnectionEvent::LocalClose:
  case ConnectionEvent::RemoteClose: {
    ENVOY_CONN_LOG_EVENT(debug, "multi_connection_cx_attempt_failed", "connection={}", *this,
                         connection_provider_->nextConnection());
    // This connection attempt has failed. If possible, start another connection attempt
    // immediately, instead of waiting for the timer.
    if (connection_provider_->hasNextConnection()) {
      ENVOY_LOG(trace, "Disabling next attempt timer.");
      next_attempt_timer_->disableTimer();
      tryAnotherConnection();
    }
    // If there is at least one more attempt running then the current attempt can be destroyed.
    if (connections_.size() > 1) {
      // Nuke this connection and associated callbacks and let a subsequent attempt proceed.
      cleanupWrapperAndConnection(wrapper);
      return;
    }
    ASSERT(connections_.size() == 1);
    // This connection attempt failed but there are no more attempts to be made, so pass
    // the failure up by setting up this connection as the final one.
    ENVOY_CONN_LOG_EVENT(debug, "multi_connection_cx_failed", "connections={}", *this,
                         connection_provider_->totalConnections());
    break;
  }
  case ConnectionEvent::ConnectedZeroRtt: {
    IS_ENVOY_BUG("Unexpected 0-RTT event received on TCP connection.");
    return;
  }
  }

  // Close all other connections and configure the final connection.
  setUpFinalConnection(event, wrapper);
}

void MultiConnectionBaseImpl::setUpFinalConnection(ConnectionEvent event,
                                                   ConnectionCallbacksWrapper* wrapper) {
  ASSERT(event != ConnectionEvent::ConnectedZeroRtt);
  connect_finished_ = true;
  ENVOY_LOG(trace, "Disabling next attempt timer due to final connection.");
  next_attempt_timer_->disableTimer();
  // Remove the proxied connection callbacks from all connections.
  for (auto& w : callbacks_wrappers_) {
    w->connection().removeConnectionCallbacks(*w);
  }

  // Close and delete any other connections.
  auto it = connections_.begin();
  while (it != connections_.end()) {
    if (it->get() != &(wrapper->connection())) {
      (*it)->close(ConnectionCloseType::NoFlush);
      dispatcher_.deferredDelete(std::move(*it));
      it = connections_.erase(it);
    } else {
      ++it;
    }
  }
  ASSERT(connections_.size() == 1);
  callbacks_wrappers_.clear();

  // Apply post-connect state to the final socket.
  for (const auto& cb : post_connect_state_.bytes_sent_callbacks_) {
    connections_[0]->addBytesSentCallback(cb);
  }

  if (event == ConnectionEvent::Connected) {
    // Apply post-connect state which is only connections which have succeeded.
    for (auto& filter : post_connect_state_.filters_) {
      connections_[0]->addFilter(filter);
    }
    for (auto& filter : post_connect_state_.write_filters_) {
      connections_[0]->addWriteFilter(filter);
    }
    for (auto& filter : post_connect_state_.read_filters_) {
      connections_[0]->addReadFilter(filter);
    }
    if (post_connect_state_.initialize_read_filters_.has_value() &&
        post_connect_state_.initialize_read_filters_.value()) {
      // initialize_read_filters_ is set to true in initializeReadFilters() only when
      // there are read filters installed. The underlying connection's initializeReadFilters()
      // will always return true when read filters are installed so this should always
      // return true.
      ASSERT(!post_connect_state_.read_filters_.empty());
      bool initialized = connections_[0]->initializeReadFilters();
      ASSERT(initialized);
    }
    if (post_connect_state_.read_disable_count_.has_value()) {
      for (int i = 0; i < post_connect_state_.read_disable_count_.value(); ++i) {
        connections_[0]->readDisable(true);
      }
    }

    if (post_connect_state_.write_buffer_.has_value()) {
      // write_buffer_ and end_stream_ are both set together in write().
      ASSERT(post_connect_state_.end_stream_.has_value());
      // If a buffer limit was set, ensure that it was applied to the connection.
      if (per_connection_state_.buffer_limits_.has_value()) {
        ASSERT(connections_[0]->bufferLimit() == per_connection_state_.buffer_limits_.value());
      }
      connections_[0]->write(*post_connect_state_.write_buffer_.value(),
                             post_connect_state_.end_stream_.value());
    }
  }

  // Add connection callbacks after moving data from the deferred write buffer so that
  // any high watermark notification is swallowed and not conveyed to the callbacks, since
  // that was already delivered to the callbacks when the data was written to the buffer.
  for (auto cb : post_connect_state_.connection_callbacks_) {
    if (cb) {
      connections_[0]->addConnectionCallbacks(*cb);
    }
  }
}

void MultiConnectionBaseImpl::cleanupWrapperAndConnection(ConnectionCallbacksWrapper* wrapper) {
  wrapper->connection().removeConnectionCallbacks(*wrapper);
  for (auto it = connections_.begin(); it != connections_.end();) {
    if (it->get() == &(wrapper->connection())) {
      (*it)->close(ConnectionCloseType::NoFlush);
      dispatcher_.deferredDelete(std::move(*it));
      it = connections_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = callbacks_wrappers_.begin(); it != callbacks_wrappers_.end();) {
    if (it->get() == wrapper) {
      it = callbacks_wrappers_.erase(it);
    } else {
      ++it;
    }
  }
}

void MultiConnectionBaseImpl::onWriteBufferLowWatermark() {
  // Only called when moving write data from the deferred write buffer to
  // the underlying connection. In this case, the connection callbacks must
  // not be notified since this should be transparent to the callbacks.
}

void MultiConnectionBaseImpl::onWriteBufferHighWatermark() {
  ASSERT(!connect_finished_);
  for (auto callback : post_connect_state_.connection_callbacks_) {
    if (callback) {
      callback->onAboveWriteBufferHighWatermark();
    }
  }
}

} // namespace Network
} // namespace Envoy
