#include "source/common/config/registry_utils.h"

namespace Envoy {
namespace Config {

void RegistryUtils::translateOpaqueConfig(const ProtobufWkt::Any& typed_config,
                                          ProtobufMessage::ValidationVisitor& validation_visitor,
                                          Protobuf::Message& out_proto) {
  static const std::string struct_type =
      ProtobufWkt::Struct::default_instance().GetDescriptor()->full_name();
  static const std::string typed_struct_type =
      xds::type::v3::TypedStruct::default_instance().GetDescriptor()->full_name();
  static const std::string legacy_typed_struct_type =
      udpa::type::v1::TypedStruct::default_instance().GetDescriptor()->full_name();

  if (!typed_config.value().empty()) {
    // Unpack methods will only use the fully qualified type name after the last '/'.
    // https://github.com/protocolbuffers/protobuf/blob/3.6.x/src/google/protobuf/any.proto#L87
    absl::string_view type = TypeUtil::typeUrlToDescriptorFullName(typed_config.type_url());

    if (type == typed_struct_type) {
      xds::type::v3::TypedStruct typed_struct;
      MessageUtil::unpackTo(typed_config, typed_struct);
      // if out_proto is expecting Struct, return directly
      if (out_proto.GetDescriptor()->full_name() == struct_type) {
        out_proto.CopyFrom(typed_struct.value());
      } else {
        // The typed struct might match out_proto, or some earlier version, let
        // MessageUtil::jsonConvert sort this out.
        MessageUtil::jsonConvert(typed_struct.value(), validation_visitor, out_proto);
      }
    } else if (type == legacy_typed_struct_type) {
      udpa::type::v1::TypedStruct typed_struct;
      MessageUtil::unpackTo(typed_config, typed_struct);
      // if out_proto is expecting Struct, return directly
      if (out_proto.GetDescriptor()->full_name() == struct_type) {
        out_proto.CopyFrom(typed_struct.value());
      } else {
        // The typed struct might match out_proto, or some earlier version, let
        // MessageUtil::jsonConvert sort this out.
        MessageUtil::jsonConvert(typed_struct.value(), validation_visitor, out_proto);
      }
    } // out_proto is expecting Struct, unpack directly
    else if (type != struct_type || out_proto.GetDescriptor()->full_name() == struct_type) {
      MessageUtil::unpackTo(typed_config, out_proto);
    } else {
      ProtobufWkt::Struct struct_config;
      MessageUtil::unpackTo(typed_config, struct_config);
      MessageUtil::jsonConvert(struct_config, validation_visitor, out_proto);
    }
  }
}

} // namespace Config
} // namespace Envoy
