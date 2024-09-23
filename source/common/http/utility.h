#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/regex.h"
#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/message.h"
#include "envoy/http/metadata_interface.h"
#include "envoy/http/query_params.h"

#include "source/common/http/exception.h"
#include "source/common/http/http_option_limits.h"
#include "source/common/http/status.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Http {
namespace Utility {

/**
 * Well-known HTTP ALPN values.
 */
class AlpnNameValues {
public:
  const std::string Http10 = "http/1.0";
  const std::string Http11 = "http/1.1";
  const std::string Http2 = "h2";
  const std::string Http2c = "h2c";
  const std::string Http3 = "h3";
};

using AlpnNames = ConstSingleton<AlpnNameValues>;

} // namespace Utility
} // namespace Http

namespace Http2 {
namespace Utility {

/**
 * Validates settings/options already set in |options| and initializes any remaining fields with
 * defaults.
 */
absl::StatusOr<envoy::config::core::v3::Http2ProtocolOptions>
initializeAndValidateOptions(const envoy::config::core::v3::Http2ProtocolOptions& options);

absl::StatusOr<envoy::config::core::v3::Http2ProtocolOptions>
initializeAndValidateOptions(const envoy::config::core::v3::Http2ProtocolOptions& options,
                             bool hcm_stream_error_set,
                             const ProtobufWkt::BoolValue& hcm_stream_error);
} // namespace Utility
} // namespace Http2
namespace Http3 {
namespace Utility {

envoy::config::core::v3::Http3ProtocolOptions
initializeAndValidateOptions(const envoy::config::core::v3::Http3ProtocolOptions& options,
                             bool hcm_stream_error_set,
                             const ProtobufWkt::BoolValue& hcm_stream_error);

} // namespace Utility
} // namespace Http3
namespace Http {
namespace Utility {

enum UrlComponents {
  UcSchema = 0,
  UcHost = 1,
  UcPort = 2,
  UcPath = 3,
  UcQuery = 4,
  UcFragment = 5,
  UcUserinfo = 6,
  UcMax = 7
};

/**
 * Given a fully qualified URL, splits the string_view provided into scheme,
 * host and path with query parameters components.
 */

class Url {
public:
  bool initialize(absl::string_view absolute_url, bool is_connect_request);
  absl::string_view scheme() const { return scheme_; }
  absl::string_view hostAndPort() const { return host_and_port_; }
  absl::string_view pathAndQueryParams() const { return path_and_query_params_; }

  void setPathAndQueryParams(absl::string_view path_and_query_params) {
    path_and_query_params_ = path_and_query_params;
  }

  /** Returns the fully qualified URL as a string. */
  std::string toString() const;

  bool containsFragment();
  bool containsUserinfo();

private:
  absl::string_view scheme_;
  absl::string_view host_and_port_;
  absl::string_view path_and_query_params_;
  uint8_t component_bitmap_;
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
  static std::string decode(absl::string_view encoded);

  /**
   * Encodes string view for storing it as a query parameter according to the
   * x-www-form-urlencoded spec:
   * https://www.w3.org/TR/html5/forms.html#application/x-www-form-urlencoded-encoding-algorithm
   * @param value supplies string to be encoded.
   * @return std::string encoded string according to
   * https://www.w3.org/TR/html5/forms.html#application/x-www-form-urlencoded-encoding-algorithm
   *
   * Summary:
   * The x-www-form-urlencoded spec mandates that all ASCII codepoints are %-encoded except the
   * following: ALPHA | DIGIT | * | - | . | _
   *
   * NOTE: the space character is encoded as %20, NOT as the + character
   */
  static std::string urlEncodeQueryParameter(absl::string_view value);

  /**
   * Exactly the same as above, but returns false when it finds a character that should be %-encoded
   * but is not.
   */
  static bool queryParameterIsUrlEncoded(absl::string_view value);

  /**
   * Decodes string view that represents URL in x-www-form-urlencoded query parameter.
   * @param encoded supplies string to be decoded.
   * @return std::string decoded string compliant with https://datatracker.ietf.org/doc/html/rfc3986
   *
   * This function decodes a query parameter assuming it is a URL. It only decodes characters
   * permitted in the URL - the unreserved and reserved character sets.
   * unreserved-set := ALPHA | DIGIT | - | . | _ | ~
   * reserved-set := sub-delims | gen-delims
   * sub-delims := ! | $ | & | ` | ( | ) | * | + | , | ; | =
   * gen-delims := : | / | ? | # | [ | ] | @
   *
   * The following characters are not decoded:
   * ASCII controls <= 0x1F, space, DEL (0x7F), extended ASCII > 0x7F
   * As well as the following characters without defined meaning in URL
   * " | < | > | \ | ^ | { | }
   * and the "pipe" `|` character
   */
  static std::string urlDecodeQueryParameter(absl::string_view encoded);

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
 * Update authority with the specified hostname.
 * @param headers headers where authority should be updated.
 * @param hostname hostname that authority should be updated with.
 * @param append_xfh append the original authority to the x-forwarded-host header.
 */
void updateAuthority(RequestHeaderMap& headers, absl::string_view hostname, bool append_xfh);

/**
 * Creates an SSL (https) redirect path based on the input host and path headers.
 * @param headers supplies the request headers.
 * @return std::string the redirect path.
 */
std::string createSslRedirectPath(const RequestHeaderMap& headers);

/**
 * Finds the start of the query string in a path
 * @param path supplies a HeaderString& to search for the query string
 * @return absl::string_view starting at the beginning of the query string,
 *         or a string_view starting at the end of the path if there was
 *         no query string.
 */
absl::string_view findQueryStringStart(const HeaderString& path);

/**
 * Returns the path without the query string.
 * @param path supplies a HeaderString& possibly containing a query string.
 * @return std::string the path without query string.
 */
std::string stripQueryString(const HeaderString& path);

/**
 * Parse a particular value out of a cookie
 * @param headers supplies the headers to get the cookie from.
 * @param key the key for the particular cookie value to return
 * @return std::string the parsed cookie value, or "" if none exists
 **/
std::string parseCookieValue(const HeaderMap& headers, const std::string& key);

/**
 * Parse cookies from header into a map.
 * @param headers supplies the headers to get cookies from.
 * @param key_filter predicate that returns true for every cookie key to be included.
 * @return absl::flat_hash_map cookie map.
 **/
absl::flat_hash_map<std::string, std::string>
parseCookies(const RequestHeaderMap& headers,
             const std::function<bool(absl::string_view)>& key_filter);

/**
 * Parse cookies from header into a map.
 * @param headers supplies the headers to get cookies from.
 * @return absl::flat_hash_map cookie map.
 **/
absl::flat_hash_map<std::string, std::string> parseCookies(const RequestHeaderMap& headers);

/**
 * Parse a particular value out of a set-cookie
 * @param headers supplies the headers to get the set-cookie from.
 * @param key the key for the particular set-cookie value to return
 * @return std::string the parsed set-cookie value, or "" if none exists
 **/
std::string parseSetCookieValue(const HeaderMap& headers, const std::string& key);

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
                               bool httponly, const Http::CookieAttributeRefVector attributes);

/**
 * Get the response status from the response headers.
 * @param headers supplies the headers to get the status from.
 * @return uint64_t the response code or returns 0 if headers are invalid.
 */
uint64_t getResponseStatus(const ResponseHeaderMap& headers);

/**
 * Get the response status from the response headers.
 * @param headers supplies the headers to get the status from.
 * @return absl::optional<uint64_t> the response code or absl::nullopt if the headers are invalid.
 */
absl::optional<uint64_t> getResponseStatusOrNullopt(const ResponseHeaderMap& headers);

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
 * @return true if this is a CONNECT request with a :protocol header present, false otherwise.
 */
bool isH3UpgradeRequest(const RequestHeaderMap& headers);

/**
 * Determine whether this is a WebSocket Upgrade request.
 * This function returns true if the following HTTP headers and values are present:
 * - Connection: Upgrade
 * - Upgrade: websocket
 */
bool isWebSocketUpgradeRequest(const RequestHeaderMap& headers);

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

// Prepared local reply after modifying headers and rewriting body.
struct PreparedLocalReply {
  bool is_grpc_request_ = false;
  bool is_head_request_ = false;
  ResponseHeaderMapPtr response_headers_;
  std::string response_body_;
  // Function to encode response headers.
  std::function<void(ResponseHeaderMapPtr&& headers, bool end_stream)> encode_headers_;
  // Function to encode the response body.
  std::function<void(Buffer::Instance& data, bool end_stream)> encode_data_;
};

using PreparedLocalReplyPtr = std::unique_ptr<PreparedLocalReply>;

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

/**
 * Prepares a locally generated response.
 *
 * @param encode_functions supplies the functions to encode response body and headers.
 * @param local_reply_data struct which keeps data related to generate reply.
 */
PreparedLocalReplyPtr prepareLocalReply(const EncodeFunctions& encode_functions,
                                        const LocalReplyData& local_reply_data);
/**
 * Encodes a prepared local reply.
 * @param is_reset boolean reference that indicates whether a stream has been reset. It is the
 *                 responsibility of the caller to ensure that this is set to false if onDestroy()
 *                 is invoked in the context of sendLocalReply().
 * @param prepared_local_reply supplies the local reply to encode.
 */
void encodeLocalReply(const bool& is_reset, PreparedLocalReplyPtr prepared_local_reply);

struct GetLastAddressFromXffInfo {
  // Last valid address pulled from the XFF header.
  Network::Address::InstanceConstSharedPtr address_;
  // Whether this address can be used to determine if it's an internal request.
  bool allow_trusted_address_checks_;
};

/**
 * Checks if the remote address is contained by one of the trusted proxy CIDRs.
 * @param remote the remote address
 * @param trusted_cidrs the list of CIDRs which are considered trusted proxies
 * @return whether the remote address is a trusted proxy
 */
bool remoteAddressIsTrustedProxy(const Envoy::Network::Address::Instance& remote,
                                 absl::Span<const Network::Address::CidrRange> trusted_cidrs);

/**
 * Retrieves the last address in the x-forwarded-header after removing all trusted proxy addresses.
 * @param request_headers supplies the request headers
 * @param trusted_cidrs the list of CIDRs which are considered trusted proxies
 * @return GetLastAddressFromXffInfo information about the last address in the XFF header.
 *         @see GetLastAddressFromXffInfo for more information.
 */
GetLastAddressFromXffInfo
getLastNonTrustedAddressFromXFF(const Http::RequestHeaderMap& request_headers,
                                absl::Span<const Network::Address::CidrRange> trusted_cidrs);

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
 * Constructs the original URI sent from the client from
 * the request headers.
 * @param request headers from the original request
 * @param length to truncate the constructed URI's path
 */
std::string buildOriginalUri(const Http::RequestHeaderMap& request_headers,
                             absl::optional<uint32_t> max_path_length);

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
 * Transforms the supplied headers from an HTTP/1 Upgrade request to an H3 style upgrade,
 * which is the same as the H2 upgrade.
 * @param headers the headers to convert.
 */
void transformUpgradeRequestFromH1toH3(RequestHeaderMap& headers);

/**
 * Transforms the supplied headers from an HTTP/1 Upgrade response to an H2 style upgrade response.
 * Changes the 101 upgrade response to a 200 for the CONNECT response.
 * @param headers the headers to convert.
 */
void transformUpgradeResponseFromH1toH2(ResponseHeaderMap& headers);

/**
 * Transforms the supplied headers from an HTTP/1 Upgrade response to an H3 style upgrade response,
 * which is the same as the H2 style upgrade.
 * @param headers the headers to convert.
 */
void transformUpgradeResponseFromH1toH3(ResponseHeaderMap& headers);

/**
 * Transforms the supplied headers from an H2 "CONNECT"-with-:protocol-header to an HTTP/1 style
 * Upgrade response.
 * @param headers the headers to convert.
 */
void transformUpgradeRequestFromH2toH1(RequestHeaderMap& headers);

/**
 * Transforms the supplied headers from an H3 "CONNECT"-with-:protocol-header to an HTTP/1 style
 * Upgrade response. Same as H2 upgrade response transform
 * @param headers the headers to convert.
 */
void transformUpgradeRequestFromH3toH1(RequestHeaderMap& headers);

/**
 * Transforms the supplied headers from an H2 "CONNECT success" to an HTTP/1 style Upgrade response.
 * The caller is responsible for ensuring this only happens on upgraded streams.
 * @param headers the headers to convert.
 * @param upgrade the HTTP Upgrade token.
 */
void transformUpgradeResponseFromH2toH1(ResponseHeaderMap& headers, absl::string_view upgrade);

/**
 * Transforms the supplied headers from an H2 "CONNECT success" to an HTTP/1 style Upgrade response.
 * The caller is responsible for ensuring this only happens on upgraded streams.
 * Same as H2 Upgrade response transform
 * @param headers the headers to convert.
 * @param upgrade the HTTP Upgrade token.
 */
void transformUpgradeResponseFromH3toH1(ResponseHeaderMap& headers, absl::string_view upgrade);

/**
 * Retrieves the route specific config. Route specific config can be in a few
 * places, that are checked in order. The first config found is returned. The
 * order is:
 * - the routeEntry() (for config that's applied on weighted clusters)
 * - the route
 * - the virtual host object
 * - the route configuration
 *
 * To use, simply:
 *
 *     const auto* config =
 *         Utility::resolveMostSpecificPerFilterConfig<ConcreteType>(stream_callbacks_);
 *
 * See notes about config's lifetime below.
 *
 * @param callbacks The stream filter callbacks to check for route configs.
 *
 * @return The route config if found. nullptr if not found. The returned
 * pointer's lifetime is the same as the matched route.
 */
template <class ConfigType>
const ConfigType* resolveMostSpecificPerFilterConfig(const Http::StreamFilterCallbacks* callbacks) {
  static_assert(std::is_base_of<Router::RouteSpecificFilterConfig, ConfigType>::value,
                "ConfigType must be a subclass of Router::RouteSpecificFilterConfig");
  ASSERT(callbacks != nullptr);
  return dynamic_cast<const ConfigType*>(callbacks->mostSpecificPerFilterConfig());
}

/**
 * Return all the available per route filter configs.
 *
 * @param callbacks The stream filter callbacks to check for route configs.
 *
 * @return The all available per route config. The returned pointers are guaranteed to be non-null
 * and their lifetime is the same as the matched route.
 */
template <class ConfigType>
absl::InlinedVector<std::reference_wrapper<const ConfigType>, 4>
getAllPerFilterConfig(const Http::StreamFilterCallbacks* callbacks) {
  ASSERT(callbacks != nullptr);

  absl::InlinedVector<std::reference_wrapper<const ConfigType>, 4> all_configs;

  for (const auto* config : callbacks->perFilterConfigs()) {
    const ConfigType* typed_config = dynamic_cast<const ConfigType*>(config);
    if (typed_config == nullptr) {
      ENVOY_LOG_MISC(debug, "Failed to retrieve the correct type of route specific filter config");
      continue;
    }
    all_configs.push_back(*typed_config);
  }

  return all_configs;
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

/**
 * It validates RetryPolicy defined in core api. It will return an error status if invalid.
 * @param retry_policy core retry policy
 */
absl::Status validateCoreRetryPolicy(const envoy::config::core::v3::RetryPolicy& retry_policy);

/**
 * It returns RetryPolicy defined in core api to route api.
 * @param retry_policy core retry policy
 * @param retry_on this specifies when retry should be invoked.
 * @return route retry policy
 */
envoy::config::route::v3::RetryPolicy
convertCoreToRouteRetryPolicy(const envoy::config::core::v3::RetryPolicy& retry_policy,
                              const std::string& retry_on);

/**
 * @param request_headers the request header to be looked into.
 * @return true if the request method is safe as defined in
 * https://www.rfc-editor.org/rfc/rfc7231#section-4.2.1
 */
bool isSafeRequest(const Http::RequestHeaderMap& request_headers);

/**
 * @param value: the value of the referer header field
 * @return true if the given value conforms to RFC specifications
 * https://www.rfc-editor.org/rfc/rfc7231#section-5.5.2
 */
bool isValidRefererValue(absl::string_view value);

/**
 * Return the GatewayTimeout HTTP code to indicate the request is full received.
 */
Http::Code maybeRequestTimeoutCode(bool remote_decode_complete);

/**
 * Container for route config elements that pertain to a redirect.
 */
struct RedirectConfig {
  const std::string scheme_redirect_;
  const std::string host_redirect_;
  const std::string port_redirect_;
  const std::string path_redirect_;
  const std::string prefix_rewrite_redirect_;
  const std::string regex_rewrite_redirect_substitution_;
  Regex::CompiledMatcherPtr regex_rewrite_redirect_;
  // Keep small members (bools and enums) at the end of class, to reduce alignment overhead.
  const bool path_redirect_has_query_;
  const bool https_redirect_;
  const bool strip_query_;
};

/**
 * Validates the provided scheme is valid (either http or https)
 * @param scheme the scheme to validate
 * @return bool true if the scheme is valid.
 */
bool schemeIsValid(const absl::string_view scheme);

/**
 * @param scheme the scheme to validate
 * @return bool true if the scheme is http.
 */
bool schemeIsHttp(const absl::string_view scheme);

/**
 * @param scheme the scheme to validate
 * @return bool true if the scheme is https.
 */
bool schemeIsHttps(const absl::string_view scheme);

/*
 * Compute new path based on RedirectConfig.
 */
std::string newUri(::Envoy::OptRef<const RedirectConfig> redirect_config,
                   const Http::RequestHeaderMap& headers);

} // namespace Utility
} // namespace Http
} // namespace Envoy
