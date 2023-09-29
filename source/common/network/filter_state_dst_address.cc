#include "source/common/network/filter_state_dst_address.h"

#include "source/common/network/utility.h"

namespace Envoy {
namespace Network {

absl::optional<uint64_t> AddressObject::hash() const {
  return HashUtil::xxHash64(address_->asStringView());
}

class AddressObjectReflection : public StreamInfo::FilterState::ObjectReflection {
public:
  AddressObjectReflection(const AddressObject* object) : object_(object) {}
  FieldType getField(absl::string_view field_name) const override {
    const auto* ip = object_->address_->ip();
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

private:
  const AddressObject* object_;
};

std::unique_ptr<StreamInfo::FilterState::Object>
BaseAddressObjectFactory::createFromBytes(absl::string_view data) const {
  const auto address = Utility::parseInternetAddressAndPortNoThrow(std::string(data));
  return address ? std::make_unique<AddressObject>(address) : nullptr;
}
std::unique_ptr<StreamInfo::FilterState::ObjectReflection>
BaseAddressObjectFactory::reflect(const StreamInfo::FilterState::Object* data) const {
  const auto* object = dynamic_cast<const AddressObject*>(data);
  if (object) {
    return std::make_unique<AddressObjectReflection>(object);
  }
  return nullptr;
}

} // namespace Network
} // namespace Envoy
