#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/http/codes.h"
#include "envoy/http/filter.h"

#include "common/json/json_loader.h"

#include "api/protocol.pb.h"

namespace Envoy {
namespace Http {

/**
 * General HTTP utilities.
 */
class Utility {
public:
  typedef std::map<std::string, std::string> QueryParams;

  /**
   * Append to x-forwarded-for header.
   * @param headers supplies the headers to append to.
   * @param remote_address supplies the remote address to append.
   */
  static void appendXff(HeaderMap& headers, const Network::Address::Instance& remote_address);

  /**
   * Creates an SSL (https) redirect path based on the input host and path headers.
   * @param headers supplies the request headers.
   * @return std::string the redirect path.
   */
  static std::string createSslRedirectPath(const HeaderMap& headers);

  /**
   * Parse a URL into query parameters.
   * @param url supplies the url to parse.
   * @return QueryParams the parsed parameters, if any.
   */
  static QueryParams parseQueryString(const std::string& url);

  /**
   * Finds the start of the query string in a path
   * @param path supplies a HeaderString& to search for the query string
   * @return const char* a pointer to the beginning of the query string, or the end of the
   *         path if there is no query
   */
  static const char* findQueryStringStart(const HeaderString& path);

  /**
   * Parse a particular value out of a cookie
   * @param headers supplies the headers to get the cookie from.
   * @param key the key for the particular cookie value to return
   * @return std::string the parsed cookie value, or "" if none exists
   **/
  static std::string parseCookieValue(const HeaderMap& headers, const std::string& key);

  /**
   * Check whether a Set-Cookie header for the given cookie name exists
   * @param headers supplies the headers to search for the cookie
   * @param key the name of the cookie to search for
   * @return bool true if the cookie is set, false otherwise
   */
  static bool hasSetCookie(const HeaderMap& headers, const std::string& key);

  /**
   * Produce the value for a Set-Cookie header with the given parameters.
   * @param key is the name of the cookie that is being set.
   * @param value the value to set the cookie to; this value is trusted.
   * @param max_age the length of time for which the cookie is valid.
   * @return std::string a valid Set-Cookie header value string
   */
  static std::string makeSetCookieValue(const std::string& key, const std::string& value,
                                        const std::chrono::seconds max_age);

  /**
   * Get the response status from the response headers.
   * @param headers supplies the headers to get the status from.
   * @return uint64_t the response code or throws an exception if the headers are invalid.
   */
  static uint64_t getResponseStatus(const HeaderMap& headers);

  /**
   * Determine whether this is an internal origin request by parsing out x-forwarded-for from
   * HTTP headers. Currently this returns true IFF the following holds true:
   * - There is a single XFF header
   * - There is a single address in the single XFF header
   * - The address is an RFC1918 address.
   */
  static bool isInternalRequest(const HeaderMap& headers);

  /**
   * Determine whether this is a WebSocket Upgrade request.
   * This function returns true if the following HTTP headers and values are present:
   * - Connection: Upgrade
   * - Upgrade: websocket
   */
  static bool isWebSocketUpgradeRequest(const HeaderMap& headers);

  /**
   * @return Http2Settings An Http2Settings populated from the envoy::api::v2::Http2ProtocolOptions
   *         config.
   */
  static Http2Settings parseHttp2Settings(const envoy::api::v2::Http2ProtocolOptions& config);

  /**
   * @return Http1Settings An Http1Settings populated from the envoy::api::v2::Http1ProtocolOptions
   *         config.
   */
  static Http1Settings parseHttp1Settings(const envoy::api::v2::Http1ProtocolOptions& config);

  /**
   * Create a locally generated response using filter callbacks.
   * @param callbacks supplies the filter callbacks to use.
   * @param is_reset boolean reference that indicates whether a stream has been reset. It is the
   *                 responsibility of the caller to ensure that this is set to false if onDestroy()
   *                 is invoked in the context of sendLocalReply().
   * @param response_code supplies the HTTP response code.
   * @param body_text supplies the optional body text which is sent using the text/plain content
   *                  type.
   */
  static void sendLocalReply(StreamDecoderFilterCallbacks& callbacks, const bool& is_reset,
                             Code response_code, const std::string& body_text);
  /**
   * Create a locally generated response using the provided lambdas.
   * @param encode_headers supplies the function to encode response headers.
   * @param encode_data supplies the function to encode the response body.
   * @param is_reset boolean reference that indicates whether a stream has been reset. It is the
   *                 responsibility of the caller to ensure that this is set to false if onDestroy()
   *                 is invoked in the context of sendLocalReply().
   * @param response_code supplies the HTTP response code.
   * @param body_text supplies the optional body text which is sent using the text/plain content
   *                  type.
   */
  static void
  sendLocalReply(std::function<void(HeaderMapPtr&& headers, bool end_stream)> encode_headers,
                 std::function<void(Buffer::Instance& data, bool end_stream)> encode_data,
                 const bool& is_reset, Code response_code, const std::string& body_text);

  /**
   * Send a redirect response (301).
   * @param callbacks supplies the filter callbacks to use.
   * @param new_path supplies the redirect target.
   */
  static void sendRedirect(StreamDecoderFilterCallbacks& callbacks, const std::string& new_path);

  /**
   * Retrieves the last address in x-forwarded-for header. If it isn't set, returns empty string.
   * @param request_headers
   * @return last_address_in_xff
   */
  static std::string getLastAddressFromXFF(const Http::HeaderMap& request_headers);
};

} // namespace Http
} // namespace Envoy
