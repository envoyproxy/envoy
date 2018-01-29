#pragma once

#include <string>

#include "envoy/api/v2/base.pb.h"

#include "common/protobuf/protobuf.h"
#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Config {

/**
 * Config metadata helpers.
 */
class Metadata {
public:
  /**
   * Lookup value of a key for a given filter in Metadata.
   * @param metadata reference.
   * @param filter name.
   * @param key for filter metadata.
   * @return const ProtobufWkt::Value& value if found, empty if not found.
   */
  static const ProtobufWkt::Value& metadataValue(const envoy::api::v2::Metadata& metadata,
                                                 const std::string& filter, const std::string& key);

  /**
   * Obtain mutable reference to metadata value for a given filter and key.
   * @param metadata reference.
   * @param filter name.
   * @param key for filter metadata.
   * @return ProtobufWkt::Value&. A Value message is created if not found.
   */
  static ProtobufWkt::Value& mutableMetadataValue(envoy::api::v2::Metadata& metadata,
                                                  const std::string& filter,
                                                  const std::string& key);
};

} // namespace Config
} // namespace Envoy
