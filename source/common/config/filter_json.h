#pragma once

#include "envoy/config/filter/http/buffer/v2/buffer.pb.h"
#include "envoy/config/filter/http/fault/v2/fault.pb.h"
#include "envoy/config/filter/http/health_check/v2/health_check.pb.h"
#include "envoy/config/filter/http/lua/v2/lua.pb.h"
#include "envoy/config/filter/http/rate_limit/v2/rate_limit.pb.h"
#include "envoy/config/filter/http/router/v2/router.pb.h"
#include "envoy/config/filter/http/squash/v2/squash.pb.h"
#include "envoy/config/filter/http/transcoder/v2/transcoder.pb.h"
#include "envoy/config/filter/network/client_ssl_auth/v2/client_ssl_auth.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/config/filter/network/mongo_proxy/v2/mongo_proxy.pb.h"
#include "envoy/config/filter/network/rate_limit/v2/rate_limit.pb.h"
#include "envoy/config/filter/network/redis_proxy/v2/redis_proxy.pb.h"
#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.h"
#include "envoy/json/json_object.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Config {

class FilterJson {
public:
  /**
   * Translate a v1 JSON access log filter object to v2
   * envoy::config::filter::accesslog::v2::AccessLogFilter.
   * @param json_config source v1 JSON access log object.
   * @param proto_config destination v2 envoy::config::filter::accesslog::v2::AccessLog.
   */
  static void
  translateAccessLogFilter(const Json::Object& json_config,
                           envoy::config::filter::accesslog::v2::AccessLogFilter& proto_config);

  /**
   * Translate a v1 JSON access log object to v2 envoy::config::filter::accesslog::v2::AccessLog.
   * @param json_config source v1 JSON access log object.
   * @param proto_config destination v2 envoy::config::filter::accesslog::v2::AccessLog.
   */
  static void translateAccessLog(const Json::Object& json_config,
                                 envoy::config::filter::accesslog::v2::AccessLog& proto_config);

  /**
   * Translate a v1 JSON HTTP connection manager object to v2
   * envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager.
   * @param json_config source v1 JSON HTTP connection manager object.
   * @param proto_config destination v2
   * envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager.
   */
  static void translateHttpConnectionManager(
      const Json::Object& json_config,
      envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
          proto_config,
      const Stats::StatsOptions& stats_options);

  /**
   * Translate a v1 JSON Redis proxy object to v2
   * envoy::config::filter::network::redis_proxy::v2::RedisProxy.
   * @param json_config source v1 JSON HTTP connection manager object.
   * @param proto_config destination v2
   * envoy::config::filter::network::redis_proxy::v2::RedisProxy.
   */
  static void
  translateRedisProxy(const Json::Object& json_config,
                      envoy::config::filter::network::redis_proxy::v2::RedisProxy& proto_config);

  /**
   * Translate a v1 JSON Mongo proxy object to v2
   * envoy::config::filter::network::mongo_proxy::v2::MongoProxy.
   * @param json_config source v1 JSON HTTP connection manager object.
   * @param proto_config destination v2
   * envoy::config::filter::network::mongo_proxy::v2::MongoProxy.
   */
  static void
  translateMongoProxy(const Json::Object& json_config,
                      envoy::config::filter::network::mongo_proxy::v2::MongoProxy& proto_config);

  /**
   * Translate a v1 JSON Fault filter object to v2
   * envoy::config::filter::http::fault::v2::HTTPFault.
   * @param json_config source v1 JSON HTTP Fault Filter object.
   * @param proto_config destination v2
   * envoy::config::filter::http::fault::v2::HTTPFault.
   */
  static void translateFaultFilter(const Json::Object& json_config,
                                   envoy::config::filter::http::fault::v2::HTTPFault& proto_config);

  /**
   * Translate a v1 JSON Health Check filter object to v2
   * envoy::config::filter::http::health_check::v2::HealthCheck.
   * @param json_config source v1 JSON Health Check Filter object.
   * @param proto_config destination v2
   * envoy::config::filter::http::health_check::v2::HealthCheck.
   */
  static void translateHealthCheckFilter(
      const Json::Object& json_config,
      envoy::config::filter::http::health_check::v2::HealthCheck& proto_config);

  /**
   * Translate a v1 JSON HTTP Grpc JSON transcoder filter object to v2
   * envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder.
   * @param json_config source v1 JSON Grpc JSON Transcoder Filter object.
   * @param proto_config destination v2
   * envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder.
   */
  static void translateGrpcJsonTranscoder(
      const Json::Object& json_config,
      envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder& proto_config);

  /**
   * Translate a v1 JSON Router object to v2 envoy::config::filter::http::router::v2::Router.
   * @param json_config source v1 JSON HTTP router object.
   * @param proto_config destination v2 envoy::config::filter::http::router::v2::Router.
   */
  static void translateRouter(const Json::Object& json_config,
                              envoy::config::filter::http::router::v2::Router& proto_config);

  /**
   * Translate a v1 JSON Buffer filter object to v2 envoy::config::filter::http::buffer::v2::Buffer.
   * @param json_config source v1 JSON HTTP Buffer Filter object.
   * @param proto_config destination v2
   * envoy::config::filter::http::buffer::v2::Buffer.
   */
  static void translateBufferFilter(const Json::Object& json_config,
                                    envoy::config::filter::http::buffer::v2::Buffer& proto_config);

  /**
   * Translate a v1 JSON Lua filter object to v2 envoy::config::filter::http::lua::v2::Lua.
   * @param json_config source v1 JSON HTTP Lua Filter object.
   * @param proto_config destination v2
   * envoy::config::filter::http::lua::v2::Lua.
   */
  static void translateLuaFilter(const Json::Object& json_config,
                                 envoy::config::filter::http::lua::v2::Lua& proto_config);

  /**
   * Translate a v1 JSON TCP proxy filter object to a v2
   * envoy::config::filter::network::tcp_proxy::v2::TcpProxy.
   * @param json_config source v1 JSON TCP proxy object.
   * @param proto_config destination v2 envoy::config::filter::network::tcp_proxy::v2::TcpProxy.
   */
  static void
  translateTcpProxy(const Json::Object& json_config,
                    envoy::config::filter::network::tcp_proxy::v2::TcpProxy& proto_config);

  /**
   * Translate a v1 JSON TCP Rate Limit filter object to v2
   * envoy::config::filter::network::rate_limit::v2::RateLimit.
   * @param json_config source v1 JSON Tcp Rate Limit Filter object.
   * @param proto_config destination v2 envoy::config::filter::network::rate_limit::v2::RateLimit.
   */
  static void translateTcpRateLimitFilter(
      const Json::Object& json_config,
      envoy::config::filter::network::rate_limit::v2::RateLimit& proto_config);

  /**
   * Translate a v1 JSON HTTP Rate Limit filter object to v2
   * envoy::config::filter::http::rate_limit::v2::RateLimit.
   * @param json_config source v1 JSON Http Rate Limit Filter object.
   * @param proto_config destination v2 envoy::config::filter::http::rate_limit::v2::RateLimit.
   */
  static void translateHttpRateLimitFilter(
      const Json::Object& json_config,
      envoy::config::filter::http::rate_limit::v2::RateLimit& proto_config);

  /**
   * Translate a v1 JSON Client SSL Auth filter object to v2
   * envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth.
   * @param json_config source v1 JSON Client SSL Auth Filter object.
   * @param proto_config destination v2
   * envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth.
   */
  static void translateClientSslAuthFilter(
      const Json::Object& json_config,
      envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth& proto_config);

  /**
   * Translate a v1 JSON SquashConfig object to v2 envoy::config::filter::http::squash::v2::Squash.
   * @param json_config source v1 JSON HTTP SquashConfig object.
   * @param proto_config destination v2 envoy::config::filter::http::squash::v2::Squash.
   */
  static void translateSquashConfig(const Json::Object& json_config,
                                    envoy::config::filter::http::squash::v2::Squash& proto_config);
};
} // namespace Config
} // namespace Envoy
