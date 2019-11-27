#pragma once

#include "common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

class ApiTypeDb {
public:
  /**
   * Based on the presented extension config and name, determine if this is
   * configuration for an earlier version than the latest alpha version
   * supported by Envoy internally. If so, return the descriptor for the earlier
   * message, to support upgrading via VersionConverter::upgrade().
   *
   * @param extension_name name of extension corresponding to config.
   * @param typed_config opaque config packed in google.protobuf.Any.
   * @param target_type target type of conversion.
   * @return const Protobuf::Descriptor* descriptor for earlier message version
   *         corresponding to config, if any, otherwise nullptr.
   */
  static const Protobuf::Descriptor*
  inferEarlierVersionDescriptor(absl::string_view extension_name,
                                const ProtobufWkt::Any& typed_config,
                                absl::string_view target_type);
};

} // namespace Config
} // namespace Envoy
