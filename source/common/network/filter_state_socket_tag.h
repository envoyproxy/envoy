#pragma once

#include "envoy/network/socket_tag.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

/**
 * A socket tag which can be applied to a socket.
 */
class FilterStateSocketTag : public StreamInfo::FilterState::Object {
public:
  FilterStateSocketTag(Network::SocketTagSharedPtr socket_tag) : socket_tag_(socket_tag) {}

  const Network::SocketTagSharedPtr& value() const { return socket_tag_; }
  static const std::string& key();

private:
  Network::SocketTagSharedPtr socket_tag_;
};

} // namespace Network
} // namespace
