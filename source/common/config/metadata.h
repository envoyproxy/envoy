#pragma once

#include <string>

#include "envoy/api/v2/core/base.pb.h"

#include "common/protobuf/protobuf.h"

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
  static const ProtobufWkt::Value& metadataValue(const envoy::api::v2::core::Metadata& metadata,
                                                 const std::string& filter, const std::string& key);
  /**
   * Lookup value by a multi-key path for a given filter in Metadata. If path is empty
   * will return the empty struct.
   * @param metadata reference.
   * @param filter name.
   * @param path multi-key path.
   * @return const ProtobufWkt::Value& value if found, empty if not found.
   */
  static const ProtobufWkt::Value& metadataValue(const envoy::api::v2::core::Metadata& metadata,
                                                 const std::string& filter,
                                                 const std::vector<std::string>& path);
  /**
   * Obtain mutable reference to metadata value for a given filter and key.
   * @param metadata reference.
   * @param filter name.
   * @param key for filter metadata.
   * @return ProtobufWkt::Value&. A Value message is created if not found.
   */
  static ProtobufWkt::Value& mutableMetadataValue(envoy::api::v2::core::Metadata& metadata,
                                                  const std::string& filter,
                                                  const std::string& key);

  /**
   * Check whether the metadata is matched to the matcher.
   * @param matcher the matcher with the match condition.
   * @param metadata the metadata to check.
   * @return true if it's matched otherwise false.
   */
  static bool match(const envoy::api::v2::core::MetadataMatcher& matcher,
                    const envoy::api::v2::core::Metadata& metadata);
};

} // namespace Config
} // namespace Envoy
