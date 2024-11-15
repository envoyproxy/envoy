#pragma once

#include "envoy/network/address.h"
#include "envoy/stream_info/filter_state.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace StreamInfo {

/*
 * A FilterState object that wraps a network address shared pointer.
 */
class UpstreamAddress : public FilterState::Object {
public:
  static const std::string& key() {
    CONSTRUCT_ON_FIRST_USE(std::string, "envoy.stream.upstream_address");
  }

  Network::Address::InstanceConstSharedPtr address_;
};

} // namespace StreamInfo
} // namespace Envoy
