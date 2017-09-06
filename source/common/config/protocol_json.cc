#include "common/config/protocol_json.h"

#include "common/config/json_utility.h"

namespace Envoy {
namespace Config {

void ProtocolJson::translateHttp1ProtocolOptions(
    const Json::Object& json_http1_settings,
    envoy::api::v2::Http1ProtocolOptions& http1_protocol_options) {
  JSON_UTIL_SET_BOOL(json_http1_settings, http1_protocol_options, allow_absolute_url);
}

void ProtocolJson::translateHttp2ProtocolOptions(
    const Json::Object& json_http2_settings,
    envoy::api::v2::Http2ProtocolOptions& http2_protocol_options) {
  JSON_UTIL_SET_INTEGER(json_http2_settings, http2_protocol_options, hpack_table_size);
  JSON_UTIL_SET_INTEGER(json_http2_settings, http2_protocol_options, max_concurrent_streams);
  JSON_UTIL_SET_INTEGER(json_http2_settings, http2_protocol_options, initial_stream_window_size);
  JSON_UTIL_SET_INTEGER(json_http2_settings, http2_protocol_options,
                        initial_connection_window_size);
}

} // namespace Config
} // namespace Envoy
