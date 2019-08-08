#include "server/connection_handler_impl.h"

namespace Envoy {
namespace Server {

class ActiveUdpListenerConfigFactory {
public:
  virtual ~ActiveUdpListenerConfigFactory(){};

  virtual std::unique_ptr<ActiveUdpListenerFactory>
  createActiveUdpListenerFactory(const Protobuf::Message&) PURE;

  // Used to identify which udp listener to create: quic or raw udp.
  virtual std::string name() PURE;
};

} // namespace Server
} // namespace Envoy
