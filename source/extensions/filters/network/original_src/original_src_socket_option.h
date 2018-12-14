#pragma once

#include "envoy/network/address.h"
#include "envoy/network/listen_socket.h"

namespace Envoy {
namespace Network {
/**
 * A socket option implementation which allows a connection to spoof its source IP/port using
 * a provided IP address (and maybe port).
 */
class OriginalSrcSocketOption : public Socket::Option {
public:
  OriginalSrcSocketOption(Address::InstanceConstSharedPtr src_address);
  ~OriginalSrcSocketOption() {}

  /**
   * @param socket the socket on which to apply options.
   * @param state the current state of the socket. Significant for options that can only be
   *        set for some particular state of the socket.
   * @return true if succeeded, false otherwise.
   */
  bool setOption(Socket& socket,
                 envoy::api::v2::core::SocketOption::SocketState state) const override;

  /**
   * A specialization of setOption targeting ConnectionSockets.
   * @param socket the connection socket on which to apply options.
   * @param state the current state of the socket. Significant for options that can only be
   *        set for some particular state of the socket.
   * @return true if succeeded, false otherwise.
   */
  bool setOption(ConnectionSocket& socket,
                 envoy::api::v2::core::SocketOption::SocketState state) const override;
  /**
   * @param vector of bytes to which the option should append hash key data that will be used
   *        to separate connections based on the option. Any data already in the key vector must
   *        not be modified.
   */
  void hashKey(std::vector<uint8_t>& key) const override;

  static constexpr uint8_t IPV4_KEY = 0;
  static constexpr uint8_t IPV6_KEY = 1;

private:
  Address::InstanceConstSharedPtr src_address_;
  Socket::Options options_to_apply_;
};

} // namespace Network
} // namespace Envoy
