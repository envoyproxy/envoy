#pragma once

#include "envoy/json/json_object.h"

#include "api/protocol.pb.h"

namespace Envoy {
namespace Config {

class ProtocolJson {
public:
  /**
   * Translate v1 JSON HTTP/2 options and settings to v2 envoy::api::v2::Http2ProtocolOptions.
   * @param json_http_codec_options source v1 HTTP codec options string.
   * @param json_http2_setting source v1 JSON HTTP/2 settings object.
   * @param http2_protocol_options destination v2 envoy::api::v2::Http2ProtocolOptions.
   */
  static void
  translateHttp2ProtocolOptions(const std::string& json_http_codec_options,
                                const Json::Object& json_http2_settings,
                                envoy::api::v2::Http2ProtocolOptions& http2_protocol_options);
};

} // namespace Config
} // namespace Envoy
