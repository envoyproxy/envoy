#include "common/config/protocol_json.h"

#include "common/config/json_utility.h"

namespace Envoy {
namespace Config {

void ProtocolJson::translateHttp2ProtocolOptions(
    const std::string& json_http_codec_options, const Json::Object& json_http2_settings,
    envoy::api::v2::Http2ProtocolOptions& http2_protocol_options) {
  for (const auto& field : {"hpack_table_size", "max_concurrent_streams",
                            "initial_stream_window_size", "initial_connection_window_size"}) {
    JsonUtil::setUInt32Value(json_http2_settings, http2_protocol_options, field);
  }
  if (json_http_codec_options == "no_compression") {
    if (http2_protocol_options.hpack_table_size().value() != 0) {
      throw EnvoyException(
          "'http_codec_options.no_compression' conflicts with 'http2_settings.hpack_table_size'");
    }
    http2_protocol_options.mutable_hpack_table_size()->set_value(0);
  }
}

} // namespace Config
} // namespace Envoy
