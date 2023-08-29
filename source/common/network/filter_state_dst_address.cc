#include "source/common/network/filter_state_dst_address.h"

#include "envoy/registry/registry.h"

#include "source/common/network/utility.h"

namespace Envoy {
namespace Network {

const std::string& DestinationAddress::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.transport_socket.original_dst_address");
}

class DestinationAddressFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return DestinationAddress::key(); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    const auto address = Utility::parseInternetAddressAndPortNoThrow(std::string(data));
    return address ? std::make_unique<DestinationAddress>(address) : nullptr;
  }
};

REGISTER_FACTORY(DestinationAddressFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace Network
} // namespace Envoy
