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
  size_t type_name_start = type_url.find('/');
  if (type_name_start == std::string::npos) {
    return {};
  }
  std::string message_type = type_url.substr(type_name_start + 1);
  absl::optional<std::string> old_message_type =
      ApiTypeOracle::getEarlierVersionMessageTypeName(message_type);
  if (old_message_type.has_value()) {
    return "type.googleapis.com/" + old_message_type.value();
  }
  return {};
}

} // namespace Config
} // namespace Envoy
