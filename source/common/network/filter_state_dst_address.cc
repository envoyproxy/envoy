#include "source/common/network/filter_state_dst_address.h"

#include "envoy/registry/registry.h"

#include "source/common/network/utility.h"

namespace Envoy {
namespace Network {

const std::string& DestinationAddress::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.transport_socket.original_dst_address");
}

class DestinationAddressReflection : public StreamInfo::FilterState::ObjectReflection {
public:
  DestinationAddressReflection(const DestinationAddress* object) : object_(object) {}
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
  const DestinationAddress* object_;
};

class DestinationAddressFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return DestinationAddress::key(); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    const auto address = Utility::parseInternetAddressAndPortNoThrow(std::string(data));
    return address ? std::make_unique<DestinationAddress>(address) : nullptr;
  }
  std::unique_ptr<StreamInfo::FilterState::ObjectReflection>
  reflect(const StreamInfo::FilterState::Object* data) const override {
    const auto* object = dynamic_cast<const DestinationAddress*>(data);
    if (object) {
      return std::make_unique<DestinationAddressReflection>(object);
    }
    return nullptr;
  }
};

REGISTER_FACTORY(DestinationAddressFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace Network
} // namespace Envoy
