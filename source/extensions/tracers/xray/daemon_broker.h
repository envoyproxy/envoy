#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/network/address.h"

#include "common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

/**
 * The broker is a way to isolate the network dependency required to communicate with the X-Ray
 * daemon.
 */
class DaemonBroker {
public:
  /**
   * Sends the input string as data to the X-Ray daemon.
   * The input string is typically a JSON serialized Span.
   * This method prefixes the data with a header necessary for the daemon to accept the input.
   */
  virtual void send(const std::string& data) const PURE;

  virtual ~DaemonBroker() = default;
};

using DaemonBrokerPtr = std::unique_ptr<DaemonBroker>;

class DaemonBrokerImpl : public DaemonBroker {
public:
  /**
   * Creates a new Broker instance.
   *
   * @param daemon_endpoint The ip and port on which the X-Ray daemon is listening.
   */
  explicit DaemonBrokerImpl(const std::string& daemon_endpoint);

  void send(const std::string& data) const final;

private:
  const Network::Address::InstanceConstSharedPtr address_;
  const Network::IoHandlePtr io_handle_;
};

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
