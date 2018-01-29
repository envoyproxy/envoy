#pragma once

#include "envoy/json/json_object.h"

#include "api/base.pb.h"

namespace Envoy {
namespace Config {

class BaseJson {
public:
  /**
   * Translate a v1 JSON integer runtime object to v2 envoy::api::v2::RuntimeUInt32.
   * @param json_runtime source v1 JSON integer runtime object.
   * @param runtime destination v2 envoy::api::v2::RuntimeUInt32.
   */
  static void translateRuntimeUInt32(const Json::Object& json_runtime,
                                     envoy::api::v2::RuntimeUInt32& runtime);

  /**
   * Translate a v1 JSON header-value object to v2 envoy::api::v2::HeaderValueOption.
   * @param json_header_value source v1 JSON header-value object.
   * @param header_value_option destination v2 envoy::api::v2::HeaderValueOption.
   */
  static void translateHeaderValueOption(const Json::Object& json_header_value,
                                         envoy::api::v2::HeaderValueOption& header_value_option);
};

} // namespace Config
} // namespace Envoy
