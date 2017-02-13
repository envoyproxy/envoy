#pragma once

#include "envoy/http/codes.h"
#include "envoy/http/filter.h"

#include "common/json/json_loader.h"

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
   * Parse a particular value out of a cookie
   * @param headers supplies the headers to get the cookie from.
   * @param key the key for the particular cookie value to return
   * @return std::string the parsed cookie value, or "" if none exists
   **/
  static std::string parseCookieValue(const HeaderMap& headers, const std::string& key);

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
   * @return uint64_t parse a "http_codec_options" JSON field and turn it into a bitmask of
   *         CodecOption values.
   */
  static uint64_t parseCodecOptions(const Json::Object& config);

  /**
   * Create a locally generated response using filter callbacks.
   * @param callbacks supplies the filter callbacks to use.
   * @param response_code supplies the HTTP response code.
   * @param body_text supplies the optional body text which is sent using the text/plain content
   *                  type.
   */
  static void sendLocalReply(StreamDecoderFilterCallbacks& callbacks, Code response_code,
                             const std::string& body_text);

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

} // Http
