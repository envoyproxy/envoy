#pragma once

#include "envoy/api/v2/protocol.pb.h"
#include "envoy/json/json_object.h"

namespace Envoy {
namespace Config {

class ProtocolJson {
public:
  /**
   * Translate v1 JSON HTTP/1.1 options and settings to v2 envoy::api::v2::Http1ProtocolOptions.
   * @param json_http1_setting source v1 JSON HTTP/1.1 settings object.
   * @param http1_protocol_options destination v2 envoy::api::v2::Http1ProtocolOptions.
   */
  static void
  translateHttp1ProtocolOptions(const Json::Object& json_http1_settings,
                                envoy::api::v2::Http1ProtocolOptions& http1_protocol_options);

  /**
   * Translate v1 JSON HTTP/2 options and settings to v2 envoy::api::v2::Http2ProtocolOptions.
   * @param json_http2_setting source v1 JSON HTTP/2 settings object.
   * @param http2_protocol_options destination v2 envoy::api::v2::Http2ProtocolOptions.
   */
  static void
  translateHttp2ProtocolOptions(const Json::Object& json_http2_settings,
                                envoy::api::v2::Http2ProtocolOptions& http2_protocol_options);
};

} // namespace Config
} // namespace Envoy
