#pragma once

#include "envoy/network/address.h"
#include "envoy/stream_info/filter_state.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Network {

/**
 * Information which filters can add if they detect the stream should go
 * upstream through an HTTP/1.1 proxy.
 */
class Http11ProxyInfoFilterState : public StreamInfo::FilterState::Object {
public:
  // Returns the key for looking up the Http11ProxyInfoFilterState in the FilterState.
  static const std::string& key();

  Http11ProxyInfoFilterState(absl::string_view hostname,
                             Network::Address::InstanceConstSharedPtr address)
      : hostname_(hostname), address_(address) {}
  Network::Address::InstanceConstSharedPtr address() const { return address_; }
  const std::string& hostname() const { return hostname_; }

private:
  // The hostname of this individual request.
  const std::string hostname_;
  // The address of the proxy.
  const Network::Address::InstanceConstSharedPtr address_;
};

} // namespace Network
} // namespace Envoy
