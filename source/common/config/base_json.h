#pragma once

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/json/json_object.h"

namespace Envoy {
namespace Config {

class BaseJson {
public:
  /**
   * Translate a v1 JSON integer runtime object to v2
   * envoy::api::v2::core::RuntimeFractionalPercent.
   * @param json_runtime source v1 JSON integer runtime object.
   * @param runtime destination v2 envoy::api::v2::core::RuntimeFractionalPercent.
   */
  static void translateRuntimeFraction(const Json::Object& json_runtime,
                                       envoy::api::v2::core::RuntimeFractionalPercent& runtime);

  /**
   * Translate a v1 JSON header-value object to v2 envoy::api::v2::core::HeaderValueOption.
   * @param json_header_value source v1 JSON header-value object.
   * @param header_value_option destination v2 envoy::api::v2::core::HeaderValueOption.
   */
  static void
  translateHeaderValueOption(const Json::Object& json_header_value,
                             envoy::api::v2::core::HeaderValueOption& header_value_option);
};

} // namespace Config
} // namespace Envoy
