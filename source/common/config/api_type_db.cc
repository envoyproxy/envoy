#include "common/config/api_type_db.h"

#include "common/protobuf/utility.h"

#include "udpa/type/v1/typed_struct.pb.h"

namespace Envoy {
namespace Config {

const Protobuf::Descriptor*
ApiTypeDb::inferEarlierVersionDescriptor(absl::string_view extension_name,
                                         const ProtobufWkt::Any& typed_config,
                                         absl::string_view target_type) {
  // This is just a placeholder for where we will load from the API type
  // database.
  absl::string_view type = TypeUtil::typeUrlToDescriptorFullName(typed_config.type_url());

  udpa::type::v1::TypedStruct typed_struct;
  if (type == udpa::type::v1::TypedStruct::default_instance().GetDescriptor()->full_name()) {
    MessageUtil::unpackTo(typed_config, typed_struct);
    type = TypeUtil::typeUrlToDescriptorFullName(typed_struct.type_url());
    ENVOY_LOG_MISC(debug, "Extracted embedded type {}", type);
  }

  // We have an earlier version if (i) we know about the extension (ii) its type
  // is different (v3alpha vs. v2) and (iii) we haven't reached the end of the
  // upgrade chain.
  if (extension_name == "envoy.ip_tagging" && type != target_type &&
      target_type != "envoy.config.filter.http.ip_tagging.v2.IPTagging") {
    const Protobuf::Descriptor* desc =
        Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
            "envoy.config.filter.http.ip_tagging.v2.IPTagging");
    ASSERT(desc != nullptr);
    ENVOY_LOG_MISC(debug, "Inferred {}", desc->full_name());
    return desc;
  }

  return nullptr;
}

} // namespace Config
} // namespace Envoy
