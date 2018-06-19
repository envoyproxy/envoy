#pragma once

#include "envoy/api/v2/core/config_source.pb.h"

#include "common/common/fmt.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Secret {

class SecretManagerUtil {
public:
  virtual ~SecretManagerUtil() {}

  /**
   * Calculate hash code of ConfigSource. To identify the same ConfigSource, calculate the hash
   * code from the ConfigSource
   *
   * @param  config_source envoy::api::v2::core::ConfigSource
   * @return hash code
   */
  static std::string configSourceHash(const envoy::api::v2::core::ConfigSource& config_source) {
    std::string jsonstr;
    if (Protobuf::util::MessageToJsonString(config_source, &jsonstr).ok()) {
      auto obj = Json::Factory::loadFromString(jsonstr);
      if (obj.get() != nullptr) {
        return std::to_string(obj->hash());
      }
    }
    throw EnvoyException(
        fmt::format("Invalid ConfigSource message: {}", config_source.DebugString()));
  }
};

} // namespace Secret
} // namespace Envoy