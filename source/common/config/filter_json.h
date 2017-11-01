#pragma once

#include "envoy/json/json_object.h"

#include "api/filter/http/fault.pb.h"
#include "api/filter/http/http_connection_manager.pb.h"
#include "api/filter/network/mongo_proxy.pb.h"

namespace Envoy {
namespace Config {

class FilterJson {
public:
  /**
   * Translate a v1 JSON access log filter object to v2
   * envoy::api::v2::filter::AccessLogFilter.
   * @param json_access_log_filter source v1 JSON access log object.
   * @param access_log_filter destination v2 envoy::api::v2::filter::AccessLog.
   */
  static void translateAccessLogFilter(const Json::Object& json_access_log_filter,
                                       envoy::api::v2::filter::AccessLogFilter& access_log_filter);

  /**
   * Translate a v1 JSON access log object to v2 envoy::api::v2::filter::AccessLog.
   * @param json_access_log source v1 JSON access log object.
   * @param access_log destination v2 envoy::api::v2::filter::AccessLog.
   */
  static void translateAccessLog(const Json::Object& json_access_log,
                                 envoy::api::v2::filter::AccessLog& access_log);

  /**
   * Translate a v1 JSON HTTP connection manager object to v2
   * envoy::api::v2::filter::http::HttpConnectionManager.
   * @param json_http_connection_manager source v1 JSON HTTP connection manager object.
   * @param http_connection_manager destination v2
   * envoy::api::v2::filter::http::HttpConnectionManager.
   */
  static void translateHttpConnectionManager(
      const Json::Object& json_http_connection_manager,
      envoy::api::v2::filter::http::HttpConnectionManager& http_connection_manager);

  /**
   * Translate a v1 JSON Mongo proxy object to v2 envoy::api::v2::filter::network::MongoProxy.
   * @param json_mongo_proxy source v1 JSON HTTP connection manager object.
   * @param mongo_proxy destination v2
   * envoy::api::v2::filter::network::MongoProxy.
   */
  static void translateMongoProxy(const Json::Object& json_mongo_proxy,
                                  envoy::api::v2::filter::network::MongoProxy& mongo_proxy);

  /**
   * Translate a v1 JSON Fault filter object to v2 envoy::api::v2::filter::http::HTTPFault.
   * @param config source v1 JSON HTTP Fault Filter object.
   * @param fault destination v2
   * envoy::api::v2::filter::http::HTTPFault.
   */
  static void translateFaultFilter(const Json::Object& config,
                                   envoy::api::v2::filter::http::HTTPFault& fault);
};

} // namespace Config
} // namespace Envoy
