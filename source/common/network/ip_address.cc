#include "source/common/network/ip_address.h"

#include "envoy/registry/registry.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/network/utility.h"

namespace Envoy {
namespace Network {

namespace {
const std::string& key() { CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.ip"); }
} // namespace

/**
 * Registers the filter state object for the dynamic extension support.
 */
class BaseIPAddressObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return key(); }

  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    const auto address = Utility::parseInternetAddressNoThrow(std::string(data));
    return address ? std::make_unique<IPAddressObject>(address) : nullptr;
  };
};

REGISTER_FACTORY(BaseIPAddressObjectFactory, StreamInfo::FilterState::ObjectFactory);
} // namespace Network
} // namespace Envoy
