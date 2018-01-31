#include "common/config/base_json.h"

namespace Envoy {
namespace Config {

void BaseJson::translateRuntimeUInt32(const Json::Object& json_runtime,
                                      envoy::api::v2::core::RuntimeUInt32& runtime) {
  runtime.set_default_value(json_runtime.getInteger("default"));
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
