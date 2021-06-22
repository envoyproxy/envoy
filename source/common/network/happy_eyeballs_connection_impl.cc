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
  connection_ = std::make_unique<ClientConnectionImpl>(dispatcher_,
                                                       address_, source_address_,
                                                       socket_factory_.createTransportSocket(std::move(transport_socket_options_)),
                                                       options_);
}

HappyEyeballsConnectionImpl::~HappyEyeballsConnectionImpl() = default;

void HappyEyeballsConnectionImpl::connect() {
  connection_->connect();
}

void HappyEyeballsConnectionImpl::addWriteFilter(WriteFilterSharedPtr filter) {
  connection_->addWriteFilter(filter);
}
void HappyEyeballsConnectionImpl::addFilter(FilterSharedPtr filter) {
  connection_->addFilter(filter);
}
void HappyEyeballsConnectionImpl::addReadFilter(ReadFilterSharedPtr filter) {
  connection_->addReadFilter(filter);
}
void HappyEyeballsConnectionImpl::removeReadFilter(ReadFilterSharedPtr filter) {
  connection_->removeReadFilter(filter);
}
bool HappyEyeballsConnectionImpl::initializeReadFilters() {
  return connection_->initializeReadFilters();
}

void HappyEyeballsConnectionImpl::addBytesSentCallback(ClientConnection::BytesSentCb cb) {
  connection_->addBytesSentCallback(cb);
}
void HappyEyeballsConnectionImpl::enableHalfClose(bool enabled) {
  connection_->enableHalfClose(enabled);
}
bool HappyEyeballsConnectionImpl::isHalfCloseEnabled() {
  return connection_->isHalfCloseEnabled();
}
std::string HappyEyeballsConnectionImpl::nextProtocol() const {
  return connection_->nextProtocol();
}
void HappyEyeballsConnectionImpl::noDelay(bool enable) {
  connection_->noDelay(enable);
}
void HappyEyeballsConnectionImpl::readDisable(bool disable) {
  connection_->readDisable(disable);
}
void HappyEyeballsConnectionImpl::detectEarlyCloseWhenReadDisabled(bool value) {
  connection_->detectEarlyCloseWhenReadDisabled(value);
}
bool HappyEyeballsConnectionImpl::readEnabled() const {
    return connection_->readEnabled();
}
const SocketAddressProvider& HappyEyeballsConnectionImpl::addressProvider() const {
  return connection_->addressProvider();
}
SocketAddressProviderSharedPtr HappyEyeballsConnectionImpl::addressProviderSharedPtr() const {
  return connection_->addressProviderSharedPtr();
}
absl::optional<ClientConnection::UnixDomainSocketPeerCredentials> HappyEyeballsConnectionImpl::unixSocketPeerCredentials() const {
  return connection_->unixSocketPeerCredentials();
}
Ssl::ConnectionInfoConstSharedPtr HappyEyeballsConnectionImpl::ssl() const {
  return connection_->ssl();
}
Connection::State HappyEyeballsConnectionImpl::state() const {
  return connection_->state();
}
bool HappyEyeballsConnectionImpl::connecting() const {
  return connection_->connecting();
}
void HappyEyeballsConnectionImpl::write(Buffer::Instance& data, bool end_stream) {
  connection_->write(data, end_stream);
}
void HappyEyeballsConnectionImpl::setBufferLimits(uint32_t limit) {
  connection_->setBufferLimits(limit);
}
uint32_t HappyEyeballsConnectionImpl::bufferLimit() const {
  return connection_->bufferLimit();
}
bool HappyEyeballsConnectionImpl::aboveHighWatermark() const {
  return connection_->aboveHighWatermark();
}
const ConnectionSocket::OptionsSharedPtr& HappyEyeballsConnectionImpl::socketOptions() const {
  return connection_->socketOptions();
}
absl::string_view HappyEyeballsConnectionImpl::requestedServerName() const {
  return connection_->requestedServerName();
}
StreamInfo::StreamInfo& HappyEyeballsConnectionImpl::streamInfo() {
  return connection_->streamInfo();
}
const StreamInfo::StreamInfo& HappyEyeballsConnectionImpl::streamInfo() const {
  return connection_->streamInfo();
}
absl::string_view HappyEyeballsConnectionImpl::transportFailureReason() const {
  return connection_->transportFailureReason();
}
bool HappyEyeballsConnectionImpl::startSecureTransport() {
  return connection_->startSecureTransport();
}
absl::optional<std::chrono::milliseconds> HappyEyeballsConnectionImpl::lastRoundTripTime() const {
  return connection_->lastRoundTripTime();
}
void HappyEyeballsConnectionImpl::addConnectionCallbacks(ConnectionCallbacks& cb) {
  connection_->addConnectionCallbacks(cb);
}
void HappyEyeballsConnectionImpl::removeConnectionCallbacks(ConnectionCallbacks& cb) {
  connection_->removeConnectionCallbacks(cb);
}
void HappyEyeballsConnectionImpl::close(ConnectionCloseType type) {
  connection_->close(type);
}
Event::Dispatcher& HappyEyeballsConnectionImpl::dispatcher() {
  return connection_->dispatcher();
}
uint64_t HappyEyeballsConnectionImpl::id() const {
  return connection_->id();
}
void HappyEyeballsConnectionImpl::hashKey(std::vector<uint8_t>& hash) const {
  connection_->hashKey(hash);
}
void HappyEyeballsConnectionImpl::setConnectionStats(const ConnectionStats& stats) {
  connection_->setConnectionStats(stats);
}
void HappyEyeballsConnectionImpl::setDelayedCloseTimeout(std::chrono::milliseconds timeout) {
  connection_->setDelayedCloseTimeout(timeout);
}

// ScopeTrackedObject
void HappyEyeballsConnectionImpl::dumpState(std::ostream& os, int indent_level) const {
  connection_->dumpState(os, indent_level);
}

} // namespace Network
} // namespace Envoy
