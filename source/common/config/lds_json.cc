#include "common/config/lds_json.h"

#include "common/common/assert.h"
#include "common/config/address_json.h"
#include "common/config/json_utility.h"
#include "common/config/tls_context_json.h"
#include "common/config/utility.h"
#include "common/config/well_known_names.h"
#include "common/json/config_schemas.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Config {

void LdsJson::translateListener(const Json::Object& json_listener,
                                envoy::api::v2::Listener& listener) {
  json_listener.validateSchema(Json::Schema::LISTENER_SCHEMA);

  const std::string name = json_listener.getString("name", "");
  Utility::checkObjNameLength("Invalid listener name", name);
  listener.set_name(name);

  AddressJson::translateAddress(json_listener.getString("address"), true, true,
                                *listener.mutable_address());

  auto* filter_chain = listener.mutable_filter_chains()->Add();
  if (json_listener.hasObject("ssl_context")) {
    TlsContextJson::translateDownstreamTlsContext(*json_listener.getObject("ssl_context"),
                                                  *filter_chain->mutable_tls_context());
  }

  for (const auto& json_filter : json_listener.getObjectArray("filters", true)) {
    auto* filter = filter_chain->mutable_filters()->Add();

    // Translate v1 name to v2 name.
    filter->set_name(
        Config::NetworkFilterNames::get().v1_converter_.getV2Name(json_filter->getString("name")));
    JSON_UTIL_SET_STRING(*json_filter, *filter->mutable_deprecated_v1(), type);

    const std::string json_config = "{\"deprecated_v1\": true, \"value\": " +
                                    json_filter->getObject("config")->asJsonString() + "}";

    const auto status = Protobuf::util::JsonStringToMessage(json_config, filter->mutable_config());
    // JSON schema has already validated that this is a valid JSON object.
    ASSERT(status.ok());
  }

  const std::string drain_type = json_listener.getString("drain_type", "default");
  if (drain_type == "modify_only") {
    listener.set_drain_type(envoy::api::v2::Listener_DrainType_MODIFY_ONLY);
  } else {
    ASSERT(drain_type == "default");
  }

  JSON_UTIL_SET_BOOL(json_listener, *filter_chain, use_proxy_proto);
  JSON_UTIL_SET_BOOL(json_listener, listener, use_original_dst);
  JSON_UTIL_SET_BOOL(json_listener, *listener.mutable_deprecated_v1(), bind_to_port);
  JSON_UTIL_SET_INTEGER(json_listener, listener, per_connection_buffer_limit_bytes);
}

} // namespace Config
} // namespace Envoy
