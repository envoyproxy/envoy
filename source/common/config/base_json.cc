#include "common/config/base_json.h"

namespace Envoy {
namespace Config {

void BaseJson::translateRuntimeFraction(const Json::Object& json_runtime,
                                        envoy::api::v2::core::RuntimeFractionalPercent& runtime) {
  runtime.mutable_default_value()->set_denominator(envoy::type::FractionalPercent::HUNDRED);
  runtime.mutable_default_value()->set_numerator(json_runtime.getInteger("default"));
  runtime.set_runtime_key(json_runtime.getString("key"));
}

void BaseJson::translateHeaderValueOption(
    const Json::Object& json_header_value,
    envoy::api::v2::core::HeaderValueOption& header_value_option) {
  header_value_option.mutable_header()->set_key(json_header_value.getString("key"));
  header_value_option.mutable_header()->set_value(json_header_value.getString("value"));
}

} // namespace Config
} // namespace Envoy
