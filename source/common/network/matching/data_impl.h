#pragma once

#include "envoy/network/filter.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Network {
namespace Matching {

/**
 * Implementation of Network::MatchingData, providing connection-level data to
 * the match tree.
 */
class MatchingDataImpl : public MatchingData {
public:
  explicit MatchingDataImpl(const ConnectionSocket& socket,
                            const StreamInfo::FilterState& filter_state,
                            const envoy::config::core::v3::Metadata& dynamic_metadata)
      : socket_(socket), filter_state_(filter_state), dynamic_metadata_(dynamic_metadata) {}
  const ConnectionSocket& socket() const override { return socket_; }
  const StreamInfo::FilterState& filterState() const override { return filter_state_; }
  const envoy::config::core::v3::Metadata& dynamicMetadata() const override {
    return dynamic_metadata_;
  }

private:
  const ConnectionSocket& socket_;
  const StreamInfo::FilterState& filter_state_;
  const envoy::config::core::v3::Metadata& dynamic_metadata_;
};

/**
 * Implementation of Network::UdpMatchingData, providing UDP data to the match tree.
 * Extended to carry the raw packet buffer so matching inputs (e.g., QuicSniInput)
 * can inspect packet contents.
 */
class UdpMatchingDataImpl : public UdpMatchingData {
public:
  UdpMatchingDataImpl(const Address::Instance& local_address,
                      const Address::Instance& remote_address,
                      const Buffer::Instance& buffer)
      : local_address_(local_address), remote_address_(remote_address), buffer_(buffer) {}

  const Address::Instance& localAddress() const override { return local_address_; }
  const Address::Instance& remoteAddress() const override { return remote_address_; }
  const Buffer::Instance& data() const override { return buffer_; }

private:
  const Address::Instance& local_address_;
  const Address::Instance& remote_address_;
  const Buffer::Instance& buffer_;
};

} // namespace Matching
} // namespace Network
} // namespace Envoy
