#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/api/v2/core/http_uri.pb.h"
#include "envoy/api/v2/core/protocol.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/message.h"
#include "envoy/http/query_params.h"

#include "common/json/json_loader.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Http {
namespace Utility {

/**
 * Append to x-forwarded-for header.
 * @param headers supplies the headers to append to.
 * @param remote_address supplies the remote address to append.
 */
void appendXff(HeaderMap& headers, const Network::Address::Instance& remote_address);

/**
 * Append to via header.
 * @param headers supplies the headers to append to.
 * @param via supplies the via header to append.
 */
void appendVia(HeaderMap& headers, const std::string& via);

/**
 * Creates an SSL (https) redirect path based on the input host and path headers.
 * @param headers supplies the request headers.
 * @return std::string the redirect path.
 */
std::string createSslRedirectPath(const HeaderMap& headers);

/**
 * Parse a URL into query parameters.
 * @param url supplies the url to parse.
 * @return QueryParams the parsed parameters, if any.
 */
QueryParams parseQueryString(absl::string_view url);

/**
 * Finds the start of the query string in a path
 * @param path supplies a HeaderString& to search for the query string
 * @return const char* a pointer to the beginning of the query string, or the end of the
 *         path if there is no query
 */
const char* findQueryStringStart(const HeaderString& path);

/**
 * Parse a particular value out of a cookie
 * @param headers supplies the headers to get the cookie from.
 * @param key the key for the particular cookie value to return
 * @return std::string the parsed cookie value, or "" if none exists
 **/
std::string parseCookieValue(const HeaderMap& headers, const std::string& key);

/**
 * Check whether a Set-Cookie header for the given cookie name exists
 * @param headers supplies the headers to search for the cookie
 * @param key the name of the cookie to search for
 * @return bool true if the cookie is set, false otherwise
 */
bool hasSetCookie(const HeaderMap& headers, const std::string& key);

/**
 * Produce the value for a Set-Cookie header with the given parameters.
 * @param key is the name of the cookie that is being set.
 * @param value the value to set the cookie to; this value is trusted.
 * @param path the path for the cookie, or the empty string to not set a path.
 * @param max_age the length of time for which the cookie is valid, or zero
 * @param httponly true if the cookie should have HttpOnly appended.
 * to create a session cookie.
 * @return std::string a valid Set-Cookie header value string
 */
std::string makeSetCookieValue(const std::string& key, const std::string& value,
                               const std::string& path, const std::chrono::seconds max_age,
                               bool httponly);

/**
 * Get the response status from the response headers.
 * @param headers supplies the headers to get the status from.
 * @return uint64_t the response code or throws an exception if the headers are invalid.
 */
uint64_t getResponseStatus(const HeaderMap& headers);

/**
 * Determine whether these headers are a valid Upgrade request or response.
 * This function returns true if the following HTTP headers and values are present:
 * - Connection: Upgrade
 * - Upgrade: [any value]
 */
bool isUpgrade(const HeaderMap& headers);

/**
 * @return true if this is a CONNECT request with a :protocol header present, false otherwise.
 */
bool isH2UpgradeRequest(const HeaderMap& headers);

/**
 * Determine whether this is a WebSocket Upgrade request.
 * This function returns true if the following HTTP headers and values are present:
 * - Connection: Upgrade
 * - Upgrade: websocket
 */
bool isWebSocketUpgradeRequest(const HeaderMap& headers);

/**
 * @return Http2Settings An Http2Settings populated from the
 * envoy::api::v2::core::Http2ProtocolOptions config.
 */
Http2Settings parseHttp2Settings(const envoy::api::v2::core::Http2ProtocolOptions& config);

/**
 * @return Http1Settings An Http1Settings populated from the
 * envoy::api::v2::core::Http1ProtocolOptions config.
 */
Http1Settings parseHttp1Settings(const envoy::api::v2::core::Http1ProtocolOptions& config);

/**
 * Create a locally generated response using filter callbacks.
 * @param is_grpc tells if this is a response to a gRPC request.
 * @param callbacks supplies the filter callbacks to use.
 * @param is_reset boolean reference that indicates whether a stream has been reset. It is the
 *                 responsibility of the caller to ensure that this is set to false if onDestroy()
 *                 is invoked in the context of sendLocalReply().
 * @param response_code supplies the HTTP response code.
 * @param body_text supplies the optional body text which is sent using the text/plain content
 *                  type.
 * @param grpc_status the gRPC status code to override the httpToGrpcStatus mapping with.
 * @param is_head_request tells if this is a response to a HEAD request
 */
void sendLocalReply(bool is_grpc, StreamDecoderFilterCallbacks& callbacks, const bool& is_reset,
                    Code response_code, const std::string& body_text,
                    const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                    bool is_head_request);

/**
 * Create a locally generated response using the provided lambdas.
 * @param is_grpc tells if this is a response to a gRPC request.
 * @param encode_headers supplies the function to encode response headers.
 * @param encode_data supplies the function to encode the response body.
 * @param is_reset boolean reference that indicates whether a stream has been reset. It is the
 *                 responsibility of the caller to ensure that this is set to false if onDestroy()
 *                 is invoked in the context of sendLocalReply().
 * @param response_code supplies the HTTP response code.
 * @param body_text supplies the optional body text which is sent using the text/plain content
 *                  type.
 * @param grpc_status the gRPC status code to override the httpToGrpcStatus mapping with.
 */
void sendLocalReply(bool is_grpc,
                    std::function<void(HeaderMapPtr&& headers, bool end_stream)> encode_headers,
                    std::function<void(Buffer::Instance& data, bool end_stream)> encode_data,
                    const bool& is_reset, Code response_code, const std::string& body_text,
                    const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                    bool is_head_request = false);

struct GetLastAddressFromXffInfo {
  // Last valid address pulled from the XFF header.
  Network::Address::InstanceConstSharedPtr address_;
  // Whether this is the only address in the XFF header.
  bool single_address_;
};

/**
 * Retrieves the last IPv4/IPv6 address in the x-forwarded-for header.
 * @param request_headers supplies the request headers.
 * @param num_to_skip specifies the number of addresses at the end of the XFF header
 *        to ignore when identifying the "last" address.
 * @return GetLastAddressFromXffInfo information about the last address in the XFF header.
 *         @see GetLastAddressFromXffInfo for more information.
 */
GetLastAddressFromXffInfo getLastAddressFromXFF(const Http::HeaderMap& request_headers,
                                                uint32_t num_to_skip = 0);

/**
 * Get the string for the given http protocol.
 * @param protocol for which to return the string representation.
 * @return string representation of the protocol.
 */
const std::string& getProtocolString(const Protocol p);

/**
 * Extract host and path from a URI. The host may contain port.
 * This function doesn't validate if the URI is valid. It only parses the URI with following
 * format: scheme://host/path.
 * @param the input URI string
 * @param the output host string.
 * @param the output path string.
 */
void extractHostPathFromUri(const absl::string_view& uri, absl::string_view& host,
                            absl::string_view& path);

/**
 * Prepare headers for a HttpUri.
 */
MessagePtr prepareHeaders(const ::envoy::api::v2::core::HttpUri& http_uri);

/**
 * Serialize query-params into a string.
 */
std::string queryParamsToString(const QueryParams& query_params);

/**
 * Transforms the supplied headers from an HTTP/1 Upgrade request to an H2 style upgrade.
 * Changes the method to connection, moves the Upgrade to a :protocol header,
 * @param headers the headers to convert.
 */
void transformUpgradeRequestFromH1toH2(HeaderMap& headers);

/**
 * Transforms the supplied headers from an HTTP/1 Upgrade response to an H2 style upgrade response.
 * Changes the 101 upgrade response to a 200 for the CONNECT response.
 * @param headers the headers to convert.
 */
void transformUpgradeResponseFromH1toH2(HeaderMap& headers);

/**
 * Transforms the supplied headers from an H2 "CONNECT"-with-:protocol-header to an HTTP/1 style
 * Upgrade response.
 * @param headers the headers to convert.
 */
void transformUpgradeRequestFromH2toH1(HeaderMap& headers);

/**
 * Transforms the supplied headers from an H2 "CONNECT success" to an HTTP/1 style Upgrade response.
 * The caller is responsible for ensuring this only happens on upgraded streams.
 * @param headers the headers to convert.
 */
void transformUpgradeResponseFromH2toH1(HeaderMap& headers, absl::string_view upgrade);

/**
 * The non template implementation of resolveMostSpecificPerFilterConfig. see
 * resolveMostSpecificPerFilterConfig for docs.
 */
const Router::RouteSpecificFilterConfig*
resolveMostSpecificPerFilterConfigGeneric(const std::string& filter_name,
                                          const Router::RouteConstSharedPtr& route);

/**
 * Retreives the route specific config. Route specific config can be in a few
 * places, that are checked in order. The first config found is returned. The
 * order is:
 * - the routeEntry() (for config that's applied on weighted clusters)
 * - the route
 * - and finally from the virtual host object (routeEntry()->virtualhost()).
 *
 * To use, simply:
 *
 *     const auto* config =
 *         Utility::resolveMostSpecificPerFilterConfig<ConcreteType>(FILTER_NAME,
 * stream_callbacks_.route());
 *
 * See notes about config's lifetime below.
 *
 * @param filter_name The name of the filter who's route config should be
 * fetched.
 * @param route The route to check for route configs. nullptr routes will
 * result in nullptr being returned.
 *
 * @return The route config if found. nullptr if not found. The returned
 * pointer's lifetime is the same as the route parameter.
 */
template <class ConfigType>
const ConfigType* resolveMostSpecificPerFilterConfig(const std::string& filter_name,
                                                     const Router::RouteConstSharedPtr& route) {
  static_assert(std::is_base_of<Router::RouteSpecificFilterConfig, ConfigType>::value,
                "ConfigType must be a subclass of Router::RouteSpecificFilterConfig");
  const Router::RouteSpecificFilterConfig* generic_config =
      resolveMostSpecificPerFilterConfigGeneric(filter_name, route);
  return dynamic_cast<const ConfigType*>(generic_config);
}

/**
 * The non template implementation of traversePerFilterConfig. see
 * traversePerFilterConfig for docs.
 */
void traversePerFilterConfigGeneric(
    const std::string& filter_name, const Router::RouteConstSharedPtr& route,
    std::function<void(const Router::RouteSpecificFilterConfig&)> cb);

/**
 * Fold all the available per route filter configs, invoking the callback with each config (if
 * it is present). Iteration of the configs is in order of specificity. That means that the callback
 * will be called first for a config on a Virtual host, then a route, and finally a route entry
 * (weighted cluster). If a config is not present, the callback will not be invoked.
 */
template <class ConfigType>
void traversePerFilterConfig(const std::string& filter_name,
                             const Router::RouteConstSharedPtr& route,
                             std::function<void(const ConfigType&)> cb) {
  static_assert(std::is_base_of<Router::RouteSpecificFilterConfig, ConfigType>::value,
                "ConfigType must be a subclass of Router::RouteSpecificFilterConfig");

  traversePerFilterConfigGeneric(
      filter_name, route, [&cb](const Router::RouteSpecificFilterConfig& cfg) {
        const ConfigType* typed_cfg = dynamic_cast<const ConfigType*>(&cfg);
        if (typed_cfg != nullptr) {
          cb(*typed_cfg);
        }
      });
}

/**
 * Merge all the available per route filter configs into one. To perform the merge,
 * the reduce function will be called on each two configs until a single merged config is left.
 *
 * @param reduce The first argument for this function will be the config from the previous level
 * and the second argument is the config from the current level (the more specific one). The
 * function should merge the second argument into the first argument.
 *
 * @return The merged config.
 */
template <class ConfigType>
absl::optional<ConfigType>
getMergedPerFilterConfig(const std::string& filter_name, const Router::RouteConstSharedPtr& route,
                         std::function<void(ConfigType&, const ConfigType&)> reduce) {
  static_assert(std::is_copy_constructible<ConfigType>::value,
                "ConfigType must be copy constructible");

  absl::optional<ConfigType> merged;

  traversePerFilterConfig<ConfigType>(filter_name, route,
                                      [&reduce, &merged](const ConfigType& cfg) {
                                        if (!merged) {
                                          merged.emplace(cfg);
                                        } else {
                                          reduce(merged.value(), cfg);
                                        }
                                      });

  return merged;
}

} // namespace Utility
} // namespace Http
} // namespace Envoy
