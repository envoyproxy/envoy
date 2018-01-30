#pragma once

#include "envoy/api/v2/filter/http/buffer.pb.h"
#include "envoy/api/v2/filter/http/fault.pb.h"
#include "envoy/api/v2/filter/http/health_check.pb.h"
#include "envoy/api/v2/filter/http/lua.pb.h"
#include "envoy/api/v2/filter/http/rate_limit.pb.h"
#include "envoy/api/v2/filter/http/router.pb.h"
#include "envoy/api/v2/filter/http/squash.pb.h"
#include "envoy/api/v2/filter/http/transcoder.pb.h"
#include "envoy/api/v2/filter/network/client_ssl_auth.pb.h"
#include "envoy/api/v2/filter/network/http_connection_manager.pb.h"
#include "envoy/api/v2/filter/network/mongo_proxy.pb.h"
#include "envoy/api/v2/filter/network/rate_limit.pb.h"
#include "envoy/api/v2/filter/network/redis_proxy.pb.h"
#include "envoy/api/v2/filter/network/tcp_proxy.pb.h"
#include "envoy/json/json_object.h"

namespace Envoy {
namespace Config {

class FilterJson {
public:
  /**
   * Translate a v1 JSON access log filter object to v2
   * envoy::api::v2::filter::accesslog::AccessLogFilter.
   * @param json_config source v1 JSON access log object.
   * @param proto_config destination v2 envoy::api::v2::filter::accesslog::AccessLog.
   */
  static void
  translateAccessLogFilter(const Json::Object& json_config,
                           envoy::api::v2::filter::accesslog::AccessLogFilter& proto_config);

  /**
   * Translate a v1 JSON access log object to v2 envoy::api::v2::filter::accesslog::AccessLog.
   * @param json_config source v1 JSON access log object.
   * @param proto_config destination v2 envoy::api::v2::filter::accesslog::AccessLog.
   */
  static void translateAccessLog(const Json::Object& json_config,
                                 envoy::api::v2::filter::accesslog::AccessLog& proto_config);

  /**
   * Translate a v1 JSON HTTP connection manager object to v2
   * envoy::api::v2::filter::network::HttpConnectionManager.
   * @param json_config source v1 JSON HTTP connection manager object.
   * @param proto_config destination v2
   * envoy::api::v2::filter::network::HttpConnectionManager.
   */
  static void translateHttpConnectionManager(
      const Json::Object& json_config,
      envoy::api::v2::filter::network::HttpConnectionManager& proto_config);

  /**
   * Translate a v1 JSON Redis proxy object to v2 envoy::api::v2::filter::network::RedisProxy.
   * @param json_config source v1 JSON HTTP connection manager object.
   * @param proto_config destination v2
   * envoy::api::v2::filter::network::RedisProxy.
   */
  static void translateRedisProxy(const Json::Object& json_config,
                                  envoy::api::v2::filter::network::RedisProxy& proto_config);

  /**
   * Translate a v1 JSON Mongo proxy object to v2 envoy::api::v2::filter::network::MongoProxy.
   * @param json_config source v1 JSON HTTP connection manager object.
   * @param proto_config destination v2
   * envoy::api::v2::filter::network::MongoProxy.
   */
  static void translateMongoProxy(const Json::Object& json_config,
                                  envoy::api::v2::filter::network::MongoProxy& proto_config);

  /**
   * Translate a v1 JSON Fault filter object to v2 envoy::api::v2::filter::http::HTTPFault.
   * @param json_config source v1 JSON HTTP Fault Filter object.
   * @param proto_config destination v2
   * envoy::api::v2::filter::http::HTTPFault.
   */
  static void translateFaultFilter(const Json::Object& json_config,
                                   envoy::api::v2::filter::http::HTTPFault& proto_config);

  /**
   * Translate a v1 JSON Health Check filter object to v2 envoy::api::v2::filter::http::HealthCheck.
   * @param json_config source v1 JSON Health Check Filter object.
   * @param proto_config destination v2
   * envoy::api::v2::filter::http::HealthCheck.
   */
  static void translateHealthCheckFilter(const Json::Object& json_config,
                                         envoy::api::v2::filter::http::HealthCheck& proto_config);

  /**
   * Translate a v1 JSON HTTP Grpc JSON transcoder filter object to v2
   * envoy::api::v2::filter::http::GrpcJsonTranscoder.
   * @param json_config source v1 JSON Grpc JSON Transcoder Filter object.
   * @param proto_config destination v2 envoy::api::v2::filter::http::GrpcJsonTranscoder.
   */
  static void
  translateGrpcJsonTranscoder(const Json::Object& json_config,
                              envoy::api::v2::filter::http::GrpcJsonTranscoder& proto_config);

  /**
   * Translate a v1 JSON Router object to v2 envoy::api::v2::filter::http::Router.
   * @param json_config source v1 JSON HTTP router object.
   * @param proto_config destination v2 envoy::api::v2::filter::http::Router.
   */
  static void translateRouter(const Json::Object& json_config,
                              envoy::api::v2::filter::http::Router& proto_config);

  /**
   * Translate a v1 JSON Buffer filter object to v2 envoy::api::v2::filter::http::Buffer.
   * @param json_config source v1 JSON HTTP Buffer Filter object.
   * @param proto_config destination v2
   * envoy::api::v2::filter::http::Buffer.
   */
  static void translateBufferFilter(const Json::Object& json_config,
                                    envoy::api::v2::filter::http::Buffer& proto_config);

  /**
   * Translate a v1 JSON Lua filter object to v2 envoy::api::v2::filter::http::Lua.
   * @param json_config source v1 JSON HTTP Lua Filter object.
   * @param proto_config destination v2
   * envoy::api::v2::filter::http::Lua.
   */
  static void translateLuaFilter(const Json::Object& json_config,
                                 envoy::api::v2::filter::http::Lua& proto_config);

  /**
   * Translate a v1 JSON TCP proxy filter object to a v2 envoy::api::v2::filter::network::TcpProxy.
   * @param json_config source v1 JSON TCP proxy object.
   * @param proto_config destination v2 envoy::api::v2::filter::network::TcpProxy.
   */
  static void translateTcpProxy(const Json::Object& json_config,
                                envoy::api::v2::filter::network::TcpProxy& proto_config);

  /**
   * Translate a v1 JSON TCP Rate Limit filter object to v2
   * envoy::api::v2::filter::network::RateLimit.
   * @param json_config source v1 JSON Tcp Rate Limit Filter object.
   * @param proto_config destination v2 envoy::api::v2::filter::network::RateLimit.
   */
  static void translateTcpRateLimitFilter(const Json::Object& json_config,
                                          envoy::api::v2::filter::network::RateLimit& proto_config);

  /**
   * Translate a v1 JSON HTTP Rate Limit filter object to v2
   * envoy::api::v2::filter::http::RateLimit.
   * @param json_config source v1 JSON Http Rate Limit Filter object.
   * @param proto_config destination v2 envoy::api::v2::filter::http::RateLimit.
   */
  static void translateHttpRateLimitFilter(const Json::Object& json_config,
                                           envoy::api::v2::filter::http::RateLimit& proto_config);

  /**
   * Translate a v1 JSON Client SSL Auth filter object to v2
   * envoy::api::v2::filter::network::ClientSSLAuth.
   * @param json_config source v1 JSON Client SSL Auth Filter object.
   * @param proto_config destination v2 envoy::api::v2::filter::network::ClientSSLAuth.
   */
  static void
  translateClientSslAuthFilter(const Json::Object& json_config,
                               envoy::api::v2::filter::network::ClientSSLAuth& proto_config);

  /**
   * Translate a v1 JSON SquashConfig object to v2 envoy::api::v2::filter::http::Squash.
   * @param json_config source v1 JSON HTTP SquashConfig object.
   * @param proto_config destination v2 envoy::api::v2::filter::http::Squash.
   */
  static void translateSquashConfig(const Json::Object& json_config,
                                    envoy::api::v2::filter::http::Squash& proto_config);
};

} // namespace Config
} // namespace Envoy
