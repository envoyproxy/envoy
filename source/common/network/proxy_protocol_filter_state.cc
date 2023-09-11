#include "source/common/network/proxy_protocol_filter_state.h"

#include "envoy/registry/registry.h"

#include "source/common/common/base64.h"
#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

const std::string& ProxyProtocolFilterState::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.proxy_protocol_options");
}

class ProxyProtocolFilterStateReflection : public StreamInfo::FilterState::ObjectReflection {
public:
  ProxyProtocolFilterStateReflection(const ProxyProtocolFilterState* object) : object_(object) {}

  FieldType getField(absl::string_view tlv_type_str) const override {    
    // Specified tlv_type must be parsable as an int.
    ASSERT(!tlv_type_str.empty());
    int tlv_type;
    if (!absl::SimpleAtoi(tlv_type_str, &tlv_type)) {
      return absl::monostate();
    }

    // Check if a valid TLV type was passed in.
    if (tlv_type >= 256 || tlv_type <= 0) {
      return absl::monostate();
    }

    // Parse a TLV with the given type from the filter state.
    // (only returns first one found with given type)
    for (auto& tlv : object_->value().tlv_vector_) {
      if (tlv.type == tlv_type) {
        return Base64::encode(reinterpret_cast<const char*>(tlv.value.data()), tlv.value.size());
      }
    }
    // TLV with given type was not found.
    return absl::monostate();
  }

private:
  const ProxyProtocolFilterState* object_;
};

class ProxyProtocolFilterStateFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return ProxyProtocolFilterState::key(); }

  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view) const override {
    // Note: we do not parse the proxy protocol data from the given string because this
    // isn't relevant to the functionality of this factory.
    PANIC("not implemented");
    return nullptr;
  }

  std::unique_ptr<StreamInfo::FilterState::ObjectReflection> reflect(const StreamInfo::FilterState::Object* data) const override {
    const auto* object = dynamic_cast<const ProxyProtocolFilterState*>(data);
    if (object) {
      return std::make_unique<ProxyProtocolFilterStateReflection>(object);
    }
    return nullptr;
  }
};

REGISTER_FACTORY(ProxyProtocolFilterStateFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace Network
} // namespace Envoy
