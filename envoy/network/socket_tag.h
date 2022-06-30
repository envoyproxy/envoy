#pragma once

#include "envoy/network/io_handle.h"

namespace Envoy {
namespace Network {

// SocketTag represents a tag that can be applied to a socket. Currently only
// implemented for Android, it facilitates assigning a Android TrafficStats tag
// and UID to a socket so that future network data usage by the socket is
// attributed to the tag and UID that the socket is tagged with.
class SocketTag {
public:
  virtual ~SocketTag() = default;

  /**
   * Applies this tag to `io_handle`.
   * @param IoHandle to which this tag should be applied.
   */
  virtual void apply(IoHandle& io_handle) const PURE;

  /**
   * @param vector of bytes to which the option should append hash key data that will be used
   *        to separate connections based on the option. Any data already in the key vector must
   *        not be modified.
   */
  virtual void hashKey(std::vector<uint8_t>& key) const PURE;
};

using SocketTagPtr = std::unique_ptr<SocketTag>;
using SocketTagSharedPtr = std::shared_ptr<SocketTag>;

}  // namespace Network
}  // namespace Envoy
