#pragma once

#include <string>

#include "common/common/singleton.h"
#include "common/protobuf/protobuf.h"

#include "api/base.pb.h"

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

/**
 * Well-known metadata filter namespaces.
 */
class MetadataFilterValues {
public:
  // Filter namespace for built-in load balancer.
  const std::string ENVOY_LB = "envoy.lb";
};

typedef ConstSingleton<MetadataFilterValues> MetadataFilters;

/**
 * Keys for MetadataFilterConstants::ENVOY_LB metadata.
 */
class MetadataEnvoyLbKeyValues {
public:
  // Key in envoy.lb filter namespace for endpoint canary bool value.
  const std::string CANARY = "canary";
};

typedef ConstSingleton<MetadataEnvoyLbKeyValues> MetadataEnvoyLbKeys;

} // namespace Config
} // namespace Envoy
