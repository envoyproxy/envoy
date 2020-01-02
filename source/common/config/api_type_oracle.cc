#include "common/config/api_type_oracle.h"

#include "common/common/assert.h"
#include "common/common/logger.h"

#include "udpa/annotations/versioning.pb.h"

namespace Envoy {
namespace Config {

const Protobuf::Descriptor*
ApiTypeOracle::getEarlierVersionDescriptor(const Protobuf::Message& message) {
  const std::string target_type = message.GetDescriptor()->full_name();

  // Determine if there is an earlier API version for target_type.
  const Protobuf::Descriptor* desc =
      Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(std::string{target_type});
  if (desc == nullptr) {
    return nullptr;
  }
  if (desc->options().HasExtension(udpa::annotations::versioning)) {
    const std::string previous_target_type =
        desc->options().GetExtension(udpa::annotations::versioning).previous_message_type();
    const Protobuf::Descriptor* desc =
        Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(previous_target_type);
    ASSERT(desc != nullptr);
    return desc;
  }

  return nullptr;
}

} // namespace Config
} // namespace Envoy
