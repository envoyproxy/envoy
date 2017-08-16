#include "common/config/lds_json.h"

#include "common/common/assert.h"
#include "common/config/json_utility.h"
#include "common/config/tls_context_json.h"
#include "common/json/config_schemas.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Config {

void LdsJson::translateListener(const Json::Object& json_listener,
                                envoy::api::v2::Listener& listener) {
  json_listener.validateSchema(Json::Schema::LISTENER_SCHEMA);

  // TODO(htuch): Figure out if we really want UnresolvedAddress here...
  Network::Address::InstanceConstSharedPtr listener_address =
      Network::Utility::resolveUrl(json_listener.getString("address"));
  auto* named_address = listener.mutable_address()->mutable_named_address();
  named_address->set_address(listener_address->ip()->addressAsString());
  named_address->mutable_port()->set_value(listener_address->ip()->port());

  auto* filter_chain = listener.mutable_filter_chains()->Add();
  if (json_listener.hasObject("ssl_context")) {
    TlsContextJson::translateDownstreamTlsContext(*json_listener.getObject("ssl_context"),
                                                  *filter_chain->mutable_tls_context());
  }

  for (const auto& json_filter : json_listener.getObjectArray("filters", true)) {
    auto* filter = filter_chain->mutable_filters()->Add();
    JSON_UTIL_SET_STRING(*json_filter, *filter, name);
    JSON_UTIL_SET_STRING(*json_filter, *filter->mutable_deprecated_v1(), type);

    const auto status = Protobuf::util::JsonStringToMessage(
        json_filter->getObject("config")->asJsonString(), filter->mutable_config());
    // JSON schema has already validated that this is a valid JSON object.
    ASSERT(status.ok());
    UNREFERENCED_PARAMETER(status);
  }

  JSON_UTIL_SET_BOOL(json_listener, *filter_chain, use_proxy_proto);

  JSON_UTIL_SET_BOOL(json_listener, listener, use_original_dst);
  JSON_UTIL_SET_INTEGER(json_listener, listener, per_connection_buffer_limit_bytes);
  JSON_UTIL_SET_STRING(json_listener, listener, name);

  JSON_UTIL_SET_BOOL(json_listener, *listener.mutable_deprecated_v1(), bind_to_port);
}

} // namespace Config
} // namespace Envoy
