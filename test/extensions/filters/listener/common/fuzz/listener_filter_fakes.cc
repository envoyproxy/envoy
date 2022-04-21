#include "test/extensions/filters/listener/common/fuzz/listener_filter_fakes.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

Network::IoHandle& FakeConnectionSocket::ioHandle() { return *io_handle_; }

const Network::IoHandle& FakeConnectionSocket::ioHandle() const { return *io_handle_; }

Network::Address::Type FakeConnectionSocket::addressType() const {
  return connection_info_provider_->localAddress()->type();
}

absl::optional<Network::Address::IpVersion> FakeConnectionSocket::ipVersion() const {
  if (connection_info_provider_->localAddress() == nullptr ||
      addressType() != Network::Address::Type::Ip) {
    return absl::nullopt;
  }

  return connection_info_provider_->localAddress()->ip()->version();
}

void FakeConnectionSocket::setDetectedTransportProtocol(absl::string_view protocol) {
  transport_protocol_ = std::string(protocol);
}

absl::string_view FakeConnectionSocket::detectedTransportProtocol() const {
  return transport_protocol_;
}

void FakeConnectionSocket::setRequestedApplicationProtocols(
    const std::vector<absl::string_view>& protocols) {
  application_protocols_.clear();
  for (const auto& protocol : protocols) {
    application_protocols_.emplace_back(protocol);
  }
}

const std::vector<std::string>& FakeConnectionSocket::requestedApplicationProtocols() const {
  return application_protocols_;
}

void FakeConnectionSocket::setRequestedServerName(absl::string_view server_name) {
  server_name_ = std::string(server_name);
}

absl::string_view FakeConnectionSocket::requestedServerName() const { return server_name_; }

void FakeConnectionSocket::setJA3Hash(absl::string_view ja3_hash) {
  ja3_hash_ = std::string(ja3_hash);
}

absl::string_view FakeConnectionSocket::ja3Hash() const { return ja3_hash_; }

Api::SysCallIntResult FakeConnectionSocket::getSocketOption([[maybe_unused]] int level, int,
                                                            [[maybe_unused]] void* optval,
                                                            socklen_t*) const {
#ifdef SOL_IP
  switch (level) {
  case SOL_IPV6:
    static_cast<sockaddr_storage*>(optval)->ss_family = AF_INET6;
    break;
  case SOL_IP:
    static_cast<sockaddr_storage*>(optval)->ss_family = AF_INET;
    break;
  default:
    PANIC("reached unexpected code");
  }

  return Api::SysCallIntResult{0, 0};
#else
  // TODO: Waiting to determine if connection redirection possible, see
  // Network::Utility::getOriginalDst()
  return Api::SysCallIntResult{-1, 0};
#endif
}

absl::optional<std::chrono::milliseconds> FakeConnectionSocket::lastRoundTripTime() { return {}; }

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
