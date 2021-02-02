#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/message.h"
#include "envoy/http/metadata_interface.h"
#include "envoy/http/query_params.h"

#include "common/http/exception.h"
#include "common/http/status.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {
namespace Utility {

// This is a wrapper around dispatch calls that may throw an exception or may return an error status
// while exception removal is in migration.
// TODO(#10878): Remove this.
Http::Status exceptionToStatus(std::function<Http::Status(Buffer::Instance&)> dispatch,
                               Buffer::Instance& data);

/**
 * Well-known HTTP ALPN values.
 */
class AlpnNameValues {
public:
  const std::string Http10 = "http/1.0";
  const std::string Http11 = "http/1.1";
  const std::string Http2 = "h2";
  const std::string Http2c = "h2c";
};

using AlpnNames = ConstSingleton<AlpnNameValues>;

} // namespace Utility
} // namespace Http

namespace Http2 {
namespace Utility {

struct SettingsEntryHash {
  size_t operator()(const nghttp2_settings_entry& entry) const {
    return absl::Hash<decltype(entry.settings_id)>()(entry.settings_id);
  }
};

struct SettingsEntryEquals {
  bool operator()(const nghttp2_settings_entry& lhs, const nghttp2_settings_entry& rhs) const {
    return lhs.settings_id == rhs.settings_id;
  }
};

// Limits and defaults for `envoy::config::core::v3::Http2ProtocolOptions` protos.
struct OptionsLimits {
  // disable HPACK compression
  static const uint32_t MIN_HPACK_TABLE_SIZE = 0;
  // initial value from HTTP/2 spec, same as NGHTTP2_DEFAULT_HEADER_TABLE_SIZE from nghttp2
  static const uint32_t DEFAULT_HPACK_TABLE_SIZE = (1 << 12);
  // no maximum from HTTP/2 spec, use unsigned 32-bit maximum
  static const uint32_t MAX_HPACK_TABLE_SIZE = std::numeric_limits<uint32_t>::max();
  // TODO(jwfang): make this 0, the HTTP/2 spec minimum
  static const uint32_t MIN_MAX_CONCURRENT_STREAMS = 1;
  // defaults to maximum, same as nghttp2
  static const uint32_t DEFAULT_MAX_CONCURRENT_STREAMS = (1U << 31) - 1;
  // no maximum from HTTP/2 spec, total streams is unsigned 32-bit maximum,
  // one-side (client/server) is half that, and we need to exclude stream 0.
  // same as NGHTTP2_INITIAL_MAX_CONCURRENT_STREAMS from nghttp2
  static const uint32_t MAX_MAX_CONCURRENT_STREAMS = (1U << 31) - 1;

  // initial value from HTTP/2 spec, same as NGHTTP2_INITIAL_WINDOW_SIZE from nghttp2
  // NOTE: we only support increasing window size now, so this is also the minimum
  // TODO(jwfang): make this 0 to support decrease window size
  static const uint32_t MIN_INITIAL_STREAM_WINDOW_SIZE = (1 << 16) - 1;
  // initial value from HTTP/2 spec is 65535, but we want more (256MiB)
  static const uint32_t DEFAULT_INITIAL_STREAM_WINDOW_SIZE = 256 * 1024 * 1024;
  // maximum from HTTP/2 spec, same as NGHTTP2_MAX_WINDOW_SIZE from nghttp2
  static const uint32_t MAX_INITIAL_STREAM_WINDOW_SIZE = (1U << 31) - 1;

  // CONNECTION_WINDOW_SIZE is similar to STREAM_WINDOW_SIZE, but for connection-level window
  // TODO(jwfang): make this 0 to support decrease window size
  static const uint32_t MIN_INITIAL_CONNECTION_WINDOW_SIZE = (1 << 16) - 1;
  // nghttp2's default connection-level window equals to its stream-level,
  // our default connection-level window also equals to our stream-level
  static const uint32_t DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE = 256 * 1024 * 1024;
  static const uint32_t MAX_INITIAL_CONNECTION_WINDOW_SIZE = (1U << 31) - 1;

  // Default limit on the number of outbound frames of all types.
  static const uint32_t DEFAULT_MAX_OUTBOUND_FRAMES = 10000;
  // Default limit on the number of outbound frames of types PING, SETTINGS and RST_STREAM.
  static const uint32_t DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES = 1000;
  // Default limit on the number of consecutive inbound frames with an empty payload
  // and no end stream flag.
  static const uint32_t DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD = 1;
  // Default limit on the number of inbound frames of type PRIORITY (per stream).
  static const uint32_t DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM = 100;
  // Default limit on the number of inbound frames of type WINDOW_UPDATE (per DATA frame sent).
  static const uint32_t DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT = 10;
};

/**
 * Validates settings/options already set in |options| and initializes any remaining fields with
 * defaults.
 */
envoy::config::core::v3::Http2ProtocolOptions
initializeAndValidateOptions(const envoy::config::core::v3::Http2ProtocolOptions& options);

envoy::config::core::v3::Http2ProtocolOptions
initializeAndValidateOptions(const envoy::config::core::v3::Http2ProtocolOptions& options,
                             bool hcm_stream_error_set,
                             const Protobuf::BoolValue& hcm_stream_error);
} // namespace Utility
} // namespace Http2

namespace Http {
namespace Utility {

/**
 * Given a fully qualified URL, splits the string_view provided into scheme,
 * host and path with query parameters components.
 */
class Url {
public:
  bool initialize(absl::string_view absolute_url, bool is_connect_request);
  absl::string_view scheme() { return scheme_; }
  absl::string_view hostAndPort() { return host_and_port_; }
  absl::string_view pathAndQueryParams() { return path_and_query_params_; }

private:
  absl::string_view scheme_;
  absl::string_view host_and_port_;
  absl::string_view path_and_query_params_;
};

class PercentEncoding {
public:
  /**
   * Encodes string view to its percent encoded representation. Non-visible ASCII is always escaped,
   * in addition to a given list of reserved chars.
   *
   * @param value supplies string to be encoded.
   * @param reserved_chars list of reserved chars to escape. By default the escaped chars in
   *        https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#responses are used.
   * @return std::string percent-encoded string.
   */
  static std::string encode(absl::string_view value, absl::string_view reserved_chars = "%");

  /**
   * Decodes string view from its percent encoded representation.
   * @param encoded supplies string to be decoded.
   * @return std::string decoded string https://tools.ietf.org/html/rfc3986#section-2.1.
   */
  static std::string decode(absl::string_view value);

private:
  // Encodes string view to its percent encoded representation, with start index.
  static std::string encode(absl::string_view value, const size_t index,
                            const absl::flat_hash_set<char>& reserved_char_set);
};

/**
 * Append to x-forwarded-for header.
 * @param headers supplies the headers to append to.
 * @param remote_address supplies the remote address to append.
 */
void appendXff(RequestHeaderMap& headers, const Network::Address::Instance& remote_address);

/**
 * Append to via header.
 * @param headers supplies the headers to append to.
 * @param via supplies the via header to append.
 */
void appendVia(RequestOrResponseHeaderMap& headers, const std::string& via);

/**
 * Creates an SSL (https) redirect path based on the input host and path headers.
 * @param headers supplies the request headers.
 * @return std::string the redirect path.
 */
std::string createSslRedirectPath(const RequestHeaderMap& headers);

/**
 * Parse a URL into query parameters.
 * @param url supplies the url to parse.
 * @return QueryParams the parsed parameters, if any.
 */
QueryParams parseQueryString(absl::string_view url);

/**
 * Parse a URL into query parameters.
 * @param url supplies the url to parse.
 * @return QueryParams the parsed and percent-decoded parameters, if any.
 */
QueryParams parseAndDecodeQueryString(absl::string_view url);

/**
 * Parse a a request body into query parameters.
 * @param body supplies the body to parse.
 * @return QueryParams the parsed parameters, if any.
 */
QueryParams parseFromBody(absl::string_view body);

/**
 * Parse query parameters from a URL or body.
 * @param data supplies the data to parse.
 * @param start supplies the offset within the data.
 * @param decode_params supplies the flag whether to percent-decode the parsed parameters (both name
 *        and value). Set to false to keep the parameters encoded.
 * @return QueryParams the parsed parameters, if any.
 */
QueryParams parseParameters(absl::string_view data, size_t start, bool decode_params);

/**
 * Finds the start of the query string in a path
 * @param path supplies a HeaderString& to search for the query string
 * @return absl::string_view starting at the beginning of the query string,
 *         or a string_view starting at the end of the path if there was
 *         no query string.
 */
absl::string_view findQueryStringStart(const HeaderString& path);

/**
 * Parse a particular value out of a cookie
 * @param headers supplies the headers to get the cookie from.
 * @param key the key for the particular cookie value to return
 * @return std::string the parsed cookie value, or "" if none exists
 **/
std::string parseCookieValue(const HeaderMap& headers, const std::string& key);

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
uint64_t getResponseStatus(const ResponseHeaderMap& headers);

/**
 * Determine whether these headers are a valid Upgrade request or response.
 * This function returns true if the following HTTP headers and values are present:
 * - Connection: Upgrade
 * - Upgrade: [any value]
 */
bool isUpgrade(const RequestOrResponseHeaderMap& headers);

/**
 * @return true if this is a CONNECT request with a :protocol header present, false otherwise.
 */
bool isH2UpgradeRequest(const RequestHeaderMap& headers);

/**
 * Determine whether this is a WebSocket Upgrade request.
 * This function returns true if the following HTTP headers and values are present:
 * - Connection: Upgrade
 * - Upgrade: websocket
 */
bool isWebSocketUpgradeRequest(const RequestHeaderMap& headers);

/**
 * @return Http1Settings An Http1Settings populated from the
 * envoy::config::core::v3::Http1ProtocolOptions config.
 */
Http1Settings parseHttp1Settings(const envoy::config::core::v3::Http1ProtocolOptions& config);

Http1Settings parseHttp1Settings(const envoy::config::core::v3::Http1ProtocolOptions& config,
                                 const Protobuf::BoolValue& hcm_stream_error);

struct EncodeFunctions {
  // Function to modify locally generated response headers.
  std::function<void(ResponseHeaderMap& headers)> modify_headers_;
  // Function to rewrite locally generated response.
  std::function<void(ResponseHeaderMap& response_headers, Code& code, std::string& body,
                     absl::string_view& content_type)>
      rewrite_;
  // Function to encode response headers.
  std::function<void(ResponseHeaderMapPtr&& headers, bool end_stream)> encode_headers_;
  // Function to encode the response body.
  std::function<void(Buffer::Instance& data, bool end_stream)> encode_data_;
};

struct LocalReplyData {
  // Tells if this is a response to a gRPC request.
  bool is_grpc_;
  // Supplies the HTTP response code.
  Code response_code_;
  // Supplies the optional body text which is returned.
  absl::string_view body_text_;
  // gRPC status code to override the httpToGrpcStatus mapping with.
  const absl::optional<Grpc::Status::GrpcStatus> grpc_status_;
  // Tells if this is a response to a HEAD request.
  bool is_head_request_ = false;
};

/**
 * Create a locally generated response using filter callbacks.
 * @param is_reset boolean reference that indicates whether a stream has been reset. It is the
 *        responsibility of the caller to ensure that this is set to false if onDestroy()
 *        is invoked in the context of sendLocalReply().
 * @param callbacks supplies the filter callbacks to use.
 * @param local_reply_data struct which keeps data related to generate reply.
 */
void sendLocalReply(const bool& is_reset, StreamDecoderFilterCallbacks& callbacks,
                    const LocalReplyData& local_reply_data);

/**
 * Create a locally generated response using the provided lambdas.

 * @param is_reset boolean reference that indicates whether a stream has been reset. It is the
 *                 responsibility of the caller to ensure that this is set to false if onDestroy()
 *                 is invoked in the context of sendLocalReply().
 * @param encode_functions supplies the functions to encode response body and headers.
 * @param local_reply_data struct which keeps data related to generate reply.
 */
void sendLocalReply(const bool& is_reset, const EncodeFunctions& encode_functions,
                    const LocalReplyData& local_reply_data);

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
GetLastAddressFromXffInfo getLastAddressFromXFF(const Http::RequestHeaderMap& request_headers,
                                                uint32_t num_to_skip = 0);

/**
 * Remove any headers nominated by the Connection header
 * Sanitize the TE header if it contains unsupported values
 *
 * @param headers the client request headers
 * @return whether the headers were sanitized successfully
 */
bool sanitizeConnectionHeader(Http::RequestHeaderMap& headers);

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
 * Takes a the path component from a file:/// URI and returns a local path for file access.
 * @param file_path if we have file:///foo/bar, the file_path is foo/bar. For file:///c:/foo/bar
 *                  it is c:/foo/bar. This is not prefixed with /.
 * @return std::string with absolute path for local access, e.g. /foo/bar, c:/foo/bar.
 */
std::string localPathFromFilePath(const absl::string_view& file_path);

/**
 * Prepare headers for a HttpUri.
 */
RequestMessagePtr prepareHeaders(const envoy::config::core::v3::HttpUri& http_uri);

/**
 * Serialize query-params into a string.
 */
std::string queryParamsToString(const QueryParams& query_params);

/**
 * Returns string representation of StreamResetReason.
 */
const std::string resetReasonToString(const Http::StreamResetReason reset_reason);

/**
 * Transforms the supplied headers from an HTTP/1 Upgrade request to an H2 style upgrade.
 * Changes the method to connection, moves the Upgrade to a :protocol header,
 * @param headers the headers to convert.
 */
void transformUpgradeRequestFromH1toH2(RequestHeaderMap& headers);

/**
 * Transforms the supplied headers from an HTTP/1 Upgrade response to an H2 style upgrade response.
 * Changes the 101 upgrade response to a 200 for the CONNECT response.
 * @param headers the headers to convert.
 */
void transformUpgradeResponseFromH1toH2(ResponseHeaderMap& headers);

/**
 * Transforms the supplied headers from an H2 "CONNECT"-with-:protocol-header to an HTTP/1 style
 * Upgrade response.
 * @param headers the headers to convert.
 */
void transformUpgradeRequestFromH2toH1(RequestHeaderMap& headers);

/**
 * Transforms the supplied headers from an H2 "CONNECT success" to an HTTP/1 style Upgrade response.
 * The caller is responsible for ensuring this only happens on upgraded streams.
 * @param headers the headers to convert.
 */
void transformUpgradeResponseFromH2toH1(ResponseHeaderMap& headers, absl::string_view upgrade);

/**
 * The non template implementation of resolveMostSpecificPerFilterConfig. see
 * resolveMostSpecificPerFilterConfig for docs.
 */
const Router::RouteSpecificFilterConfig*
resolveMostSpecificPerFilterConfigGeneric(const std::string& filter_name,
                                          const Router::RouteConstSharedPtr& route);

/**
 * Retrieves the route specific config. Route specific config can be in a few
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

struct AuthorityAttributes {
  // whether parsed authority is pure ip address(IPv4/IPv6), if it is true
  // passed that are not FQDN
  bool is_ip_address_{};

  // If parsed authority has host, that is stored here.
  absl::string_view host_;

  // If parsed authority has port, that is stored here.
  absl::optional<uint16_t> port_;
};

/**
 * Parse passed authority, and get that is valid FQDN or IPv4/IPv6 address, hostname and port-name.
 * @param host host/authority
 * @param default_port If passed authority does not have port, this value is returned
 * @return hostname parse result. that includes whether host is IP Address, hostname and port-name
 */
AuthorityAttributes parseAuthority(absl::string_view host);
} // namespace Utility
} // namespace Http
} // namespace Envoy
