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
    const Protobuf::Descriptor* earlier_desc =
        Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
            desc->options().GetExtension(udpa::annotations::versioning).previous_message_type());
    ASSERT(earlier_desc != nullptr);
    return earlier_desc;
  }

  return nullptr;
}

} // namespace Config
} // namespace Envoy
