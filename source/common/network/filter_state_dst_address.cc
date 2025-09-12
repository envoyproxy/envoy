#include "source/common/network/filter_state_dst_address.h"

#include "source/common/network/utility.h"

namespace Envoy {
namespace Network {

absl::optional<uint64_t> AddressObject::hash() const {
  if (!getAddress()) {
    return absl::nullopt;
  }
  return HashUtil::xxHash64(getAddress()->asStringView());
}

StreamInfo::FilterState::Object::FieldType
AddressObject::getField(absl::string_view field_name) const {
  if (!getAddress()) {
    return {};
  }
  const auto* ip = getAddress()->ip();
  if (!ip) {
    return {};
  }
  if (field_name == "ip") {
    return ip->addressAsString();
  } else if (field_name == "port") {
    return int64_t(ip->port());
  }
  return {};
}

std::unique_ptr<StreamInfo::FilterState::Object>
BaseAddressObjectFactory::createFromBytes(absl::string_view data) const {
  const auto address = Utility::parseInternetAddressAndPortNoThrow(std::string(data));
  return address ? std::make_unique<AddressObject>(address) : nullptr;
}

} // namespace Network
} // namespace Envoy
