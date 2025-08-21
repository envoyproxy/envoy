#pragma once

#include "envoy/network/io_handle.h"
#include "envoy/network/socket.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

/**
 * Redirect records to be used in connections.
 */
class UpstreamSocketOptionsFilterState : public StreamInfo::FilterState::Object {
public:
  UpstreamSocketOptionsFilterState() {
    upstream_options_ = std::make_shared<Network::Socket::Options>();
  }
  const Network::Socket::OptionsSharedPtr value() const { return upstream_options_; }
  void addOption(Network::Socket::OptionsSharedPtr option) {
    Network::Socket::appendOptions(upstream_options_, option);
  }
  static const std::string& key();

private:
  Network::Socket::OptionsSharedPtr upstream_options_;
};

} // namespace Network
} // namespace Envoy
