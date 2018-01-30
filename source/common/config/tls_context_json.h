#pragma once

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/json/json_object.h"

namespace Envoy {
namespace Config {

class TlsContextJson {
public:
  /**
   * Translate a v1 JSON TLS context to v2 envoy::api::v2::auth::DownstreamTlsContext.
   * @param json_tls_context source v1 JSON TLS context object.
   * @param downstream_tls_context destination v2 envoy::api::v2::Cluster.
   */
  static void
  translateDownstreamTlsContext(const Json::Object& json_tls_context,
                                envoy::api::v2::auth::DownstreamTlsContext& downstream_tls_context);

  /**
   * Translate a v1 JSON TLS context to v2 envoy::api::v2::auth::UpstreamTlsContext.
   * @param json_tls_context source v1 JSON TLS context object.
   * @param upstream_tls_context destination v2 envoy::api::v2::Cluster.
   */
  static void
  translateUpstreamTlsContext(const Json::Object& json_tls_context,
                              envoy::api::v2::auth::UpstreamTlsContext& upstream_tls_context);
  /**
   * Translate a v1 JSON TLS context to v2 envoy::api::v2::auth::CommonTlsContext.
   * @param json_tls_context source v1 JSON TLS context object.
   * @param common_tls_context destination v2 envoy::api::v2::Cluster.
   */
  static void translateCommonTlsContext(const Json::Object& json_tls_context,
                                        envoy::api::v2::auth::CommonTlsContext& common_tls_context);

  /**
   * Translate a v1 JSON TLS context to v2 envoy::api::v2::auth::TlsCertificate.
   * @param json_tls_context source v1 JSON TLS context object.
   * @param common_tls_context destination v2 envoy::api::v2::auth::TlsCertificate.
   */
  static void translateTlsCertificate(const Json::Object& json_tls_context,
                                      envoy::api::v2::auth::TlsCertificate& tls_certificate);
};

} // namespace Config
} // namespace Envoy
