#include "common/config/api_type_oracle.h"

#include "udpa/annotations/versioning.pb.h"

namespace Envoy {
namespace Config {

const Protobuf::Descriptor*
ApiTypeOracle::getEarlierVersionDescriptor(const std::string& message_type) {
  const auto previous_message_string = getEarlierVersionMessageTypeName(message_type);
  if (previous_message_string != absl::nullopt) {
    const Protobuf::Descriptor* earlier_desc =
        Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
            previous_message_string.value());
    return earlier_desc;
  } else {
    return nullptr;
  }
}

const absl::optional<std::string>
ApiTypeOracle::getEarlierVersionMessageTypeName(const std::string& message_type) {
  // Determine if there is an earlier API version for message_type.
  const Protobuf::Descriptor* desc =
      Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(std::string{message_type});
  if (desc == nullptr) {
    return absl::nullopt;
  }
  if (desc->options().HasExtension(udpa::annotations::versioning)) {
    return desc->options().GetExtension(udpa::annotations::versioning).previous_message_type();
  }
  return absl::nullopt;
}

const absl::optional<std::string> ApiTypeOracle::getEarlierTypeUrl(const std::string& type_url) {
  const std::string type{TypeUtil::typeUrlToDescriptorFullName(type_url)};
  absl::optional<std::string> old_type = ApiTypeOracle::getEarlierVersionMessageTypeName(type);
  if (old_type.has_value()) {
    return TypeUtil::descriptorFullNameToTypeUrl(old_type.value());
  }
  return {};
}

} // namespace Config
} // namespace Envoy
