#pragma once

#include "envoy/json/json_object.h"

#include "api/filter/http_connection_manager.pb.h"

namespace Envoy {
namespace Config {

class FilterJson {
public:
  /**
   * Translate a v1 JSON access log filter object to v2 envoy::api::v2::filter::AccessLogFilter.
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
   * envoy::api::v2::filter::HttpConnectionManager.
   * @param json_http_connection_manager source v1 JSON HTTP connection manager object.
   * @param http_connection_manager destination v2 envoy::api::v2::filter::HttpConnectionManager.
   */
  static void translateHttpConnectionManager(
      const Json::Object& json_http_connection_manager,
      envoy::api::v2::filter::HttpConnectionManager& http_connection_manager);
};

} // namespace Config
} // namespace Envoy
