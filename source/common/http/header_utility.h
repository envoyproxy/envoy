#pragma once

#include <vector>

#include "envoy/common/matchers.h"
#include "envoy/common/regex.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/http/header_validator.h"
#include "envoy/http/protocol.h"
#include "envoy/type/v3/range.pb.h"

#include "source/common/common/matchers.h"
#include "source/common/common/regex.h"
#include "source/common/http/status.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Http {

/**
 * Classes and methods for manipulating and checking HTTP headers.
 */
class HeaderUtility {
public:
  enum class HeaderMatchType {
    Value,
    Regex,
    Range,
    Present,
    Prefix,
    Suffix,
    Contains,
    StringMatch
  };

  /**
   * Get all header values as a single string. Multiple headers are concatenated with ','.
   */
  class GetAllOfHeaderAsStringResult {
  public:
    // The ultimate result of the concatenation. If absl::nullopt, no header values were found.
    // If the final string required a string allocation, the memory is held in
    // backingString(). This allows zero allocation in the common case of a single header
    // value.
    absl::optional<absl::string_view> result() const {
      // This is safe for move/copy of this class as the backing string will be moved or copied.
      // Otherwise result_ is valid. The assert verifies that both are empty or only 1 is set.
      ASSERT((!result_.has_value() && result_backing_string_.empty()) ||
             (result_.has_value() ^ !result_backing_string_.empty()));
      return !result_backing_string_.empty() ? result_backing_string_ : result_;
    }

    const std::string& backingString() const { return result_backing_string_; }

  private:
    absl::optional<absl::string_view> result_;
    // Valid only if result_ relies on memory allocation that must live beyond the call. See above.
    std::string result_backing_string_;

    friend class HeaderUtility;
  };
  static GetAllOfHeaderAsStringResult getAllOfHeaderAsString(const HeaderMap::GetResult& header,
                                                             absl::string_view separator = ",");
  static GetAllOfHeaderAsStringResult getAllOfHeaderAsString(const HeaderMap& headers,
                                                             const Http::LowerCaseString& key,
                                                             absl::string_view separator = ",");

  // Corresponds to the present_match from the HeaderMatchSpecifier proto in the RDS API.
  class HeaderDataPresentMatch : public HeaderMatcher {
  public:
    HeaderDataPresentMatch(const envoy::config::route::v3::HeaderMatcher& config)
        : HeaderDataPresentMatch(config, config.present_match()) {}

    // Creates a new present-matcher with an explicit setting of the present field.
    HeaderDataPresentMatch(const envoy::config::route::v3::HeaderMatcher& config,
                           bool explicit_present)
        : name_(config.name()), invert_match_(config.invert_match()),
          treat_missing_as_empty_(config.treat_missing_header_as_empty()),
          present_(explicit_present) {}

    bool matchesHeaders(const HeaderMap& request_headers) const override {
      const auto header_value = getAllOfHeaderAsString(request_headers, name_);

      // If treat_missing_as_empty is false, and the header_value is empty,
      // return true iff invert_match is the same as present_.
      if (!header_value.result().has_value() && !treat_missing_as_empty_) {
        return invert_match_ == present_;
      }

      return present_ != invert_match_;
    };

  private:
    const LowerCaseString name_;
    const bool invert_match_;
    const bool treat_missing_as_empty_;
    const bool present_;
  };

  // A shared behavior for HeaderDataMatcher types (exact, regex, etc.) that
  // correspond to the HeaderMatchSpecifier proto in RDS API.
  class HeaderDataBaseImpl : public HeaderMatcher {
  public:
    HeaderDataBaseImpl(const envoy::config::route::v3::HeaderMatcher& config)
        : name_(config.name()), invert_match_(config.invert_match()),
          treat_missing_as_empty_(config.treat_missing_header_as_empty()) {}

    // HeaderMatcher
    bool matchesHeaders(const HeaderMap& request_headers) const override {
      const auto header_value = getAllOfHeaderAsString(request_headers, name_);
      // If treat_missing_as_empty_ is false and there is no header value, most
      // matchers (other than HeaderDataPresentMatch) will just return false here.
      if (!treat_missing_as_empty_ && !header_value.result().has_value()) {
        return false;
      }

      // If the header does not have value and the result is not returned in the
      // code above, it means treat_missing_as_empty_ is set to true and we should
      // treat the header value as empty.
      absl::string_view value =
          header_value.result().has_value() ? header_value.result().value() : EMPTY_STRING;
      // Execute the specific matcher's code and invert if invert_match_ is set.
      return specificMatchesHeaders(value) != invert_match_;
    };

  protected:
    // A matcher specific implementation to match the given header_value.
    virtual bool specificMatchesHeaders(absl::string_view header_value) const PURE;
    const LowerCaseString name_;
    const bool invert_match_;
    const bool treat_missing_as_empty_;
  };

  // Corresponds to the exact_match from the HeaderMatchSpecifier proto in the RDS API.
  class HeaderDataExactMatch : public HeaderDataBaseImpl {
  public:
    HeaderDataExactMatch(const envoy::config::route::v3::HeaderMatcher& config)
        : HeaderDataBaseImpl(config), expected_value_(config.exact_match()) {}

  private:
    bool specificMatchesHeaders(absl::string_view header_value) const override {
      return expected_value_.empty() || header_value == expected_value_;
    };
    const std::string expected_value_;
  };

  // Corresponds to the safe_regex_match from the HeaderMatchSpecifier proto in the RDS API.
  class HeaderDataRegexMatch : public HeaderDataBaseImpl {
  public:
    static absl::StatusOr<std::unique_ptr<HeaderDataRegexMatch>>
    create(const envoy::config::route::v3::HeaderMatcher& config,
           Server::Configuration::CommonFactoryContext& factory_context) {
      auto regex_or_error =
          Regex::Utility::parseRegex(config.safe_regex_match(), factory_context.regexEngine());
      RETURN_IF_NOT_OK_REF(regex_or_error.status());
      return std::unique_ptr<HeaderDataRegexMatch>(
          new HeaderDataRegexMatch(config, std::move(*regex_or_error)));
    }

  protected:
    HeaderDataRegexMatch(const envoy::config::route::v3::HeaderMatcher& config,
                         Regex::CompiledMatcherPtr&& regex)
        : HeaderDataBaseImpl(config), regex_(std::move(regex)) {}

  private:
    bool specificMatchesHeaders(absl::string_view header_value) const override {
      return regex_->match(header_value);
    };
    const Regex::CompiledMatcherPtr regex_;
  };

  // Corresponds to the range_match from the HeaderMatchSpecifier proto in the RDS API.
  class HeaderDataRangeMatch : public HeaderDataBaseImpl {
  public:
    HeaderDataRangeMatch(const envoy::config::route::v3::HeaderMatcher& config)
        : HeaderDataBaseImpl(config), range_start_(config.range_match().start()),
          range_end_(config.range_match().end()) {}

  private:
    bool specificMatchesHeaders(absl::string_view header_value) const override {
      int64_t header_int_value = 0;
      return absl::SimpleAtoi(header_value, &header_int_value) &&
             header_int_value >= range_start_ && header_int_value < range_end_;
    };

    const int64_t range_start_;
    const int64_t range_end_;
  };

  // Corresponds to the prefix_match from the HeaderMatchSpecifier proto in the RDS API.
  class HeaderDataPrefixMatch : public HeaderDataBaseImpl {
  public:
    HeaderDataPrefixMatch(const envoy::config::route::v3::HeaderMatcher& config)
        : HeaderDataBaseImpl(config), prefix_(config.prefix_match()) {}

  private:
    bool specificMatchesHeaders(absl::string_view header_value) const override {
      return absl::StartsWith(header_value, prefix_);
    };
    const std::string prefix_;
  };

  // Corresponds to the suffix_match from the HeaderMatchSpecifier proto in the RDS API.
  class HeaderDataSuffixMatch : public HeaderDataBaseImpl {
  public:
    HeaderDataSuffixMatch(const envoy::config::route::v3::HeaderMatcher& config)
        : HeaderDataBaseImpl(config), suffix_(config.suffix_match()) {}

  private:
    bool specificMatchesHeaders(absl::string_view header_value) const override {
      return absl::EndsWith(header_value, suffix_);
    };
    const std::string suffix_;
  };

  // Corresponds to the contains_match from the HeaderMatchSpecifier proto in the RDS API.
  class HeaderDataContainsMatch : public HeaderDataBaseImpl {
  public:
    HeaderDataContainsMatch(const envoy::config::route::v3::HeaderMatcher& config)
        : HeaderDataBaseImpl(config), expected_substr_(config.contains_match()) {}

  private:
    bool specificMatchesHeaders(absl::string_view header_value) const override {
      return absl::StrContains(header_value, expected_substr_);
    };
    const std::string expected_substr_;
  };

  // Corresponds to the string_match from the HeaderMatchSpecifier proto in the RDS API.
  class HeaderDataStringMatch : public HeaderDataBaseImpl {
  public:
    HeaderDataStringMatch(const envoy::config::route::v3::HeaderMatcher& config,
                          Server::Configuration::CommonFactoryContext& factory_context)
        : HeaderDataBaseImpl(config),
          string_match_(std::make_unique<
                        Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
              config.string_match(), factory_context)) {}

  private:
    bool specificMatchesHeaders(absl::string_view header_value) const override {
      return string_match_->match(header_value);
    };
    const Matchers::StringMatcherPtr string_match_;
  };

  using HeaderDataPtr = std::unique_ptr<HeaderMatcher>;

  /**
   * Creates a Header Data matcher data structure according to the given HeaderMatcher config.
   * HeaderMatcher will consist of:
   *   header_match_specifier which can be any one of exact_match, regex_match, range_match,
   *   present_match, prefix_match or suffix_match.
   *   Each of these also can be inverted with the invert_match option.
   *   Absence of these options implies empty header value match based on header presence.
   *   a.exact_match: value will be used for exact string matching.
   *   b.regex_match: Match will succeed if header value matches the value specified here.
   *   c.range_match: Match will succeed if header value lies within the range specified
   *     here, using half open interval semantics [start,end).
   *   d.present_match: Match will succeed if the header is present.
   *   f.prefix_match: Match will succeed if header value matches the prefix value specified here.
   *   g.suffix_match: Match will succeed if header value matches the suffix value specified here.
   */
  static HeaderDataPtr
  createHeaderData(const envoy::config::route::v3::HeaderMatcher& config,
                   Server::Configuration::CommonFactoryContext& factory_context);

  /**
   * Build a vector of HeaderDataPtr given input config.
   */
  static std::vector<HeaderUtility::HeaderDataPtr> buildHeaderDataVector(
      const Protobuf::RepeatedPtrField<envoy::config::route::v3::HeaderMatcher>& header_matchers,
      Server::Configuration::CommonFactoryContext& context) {
    std::vector<HeaderUtility::HeaderDataPtr> ret;
    for (const auto& header_matcher : header_matchers) {
      ret.emplace_back(HeaderUtility::createHeaderData(header_matcher, context));
    }
    return ret;
  }

  /**
   * Build a vector of HeaderMatcherSharedPtr given input config.
   */
  static std::vector<Http::HeaderMatcherSharedPtr> buildHeaderMatcherVector(
      const Protobuf::RepeatedPtrField<envoy::config::route::v3::HeaderMatcher>& header_matchers,
      Server::Configuration::CommonFactoryContext& context) {
    std::vector<Http::HeaderMatcherSharedPtr> ret;
    for (const auto& header_matcher : header_matchers) {
      ret.emplace_back(HeaderUtility::createHeaderData(header_matcher, context));
    }
    return ret;
  }

  /**
   * See if the headers specified in the config are present in a request.
   * @param request_headers supplies the headers from the request.
   * @param config_headers supplies the list of configured header conditions on which to match.
   * @return bool true if all the headers (and values) in the config_headers are found in the
   *         request_headers. If no config_headers are specified, returns true.
   */
  static bool matchHeaders(const HeaderMap& request_headers,
                           const std::vector<HeaderDataPtr>& config_headers);

  /**
   * Validates that a header value is valid, according to RFC 7230, section 3.2.
   * http://tools.ietf.org/html/rfc7230#section-3.2
   * @return bool true if the header values are valid, according to the aforementioned RFC.
   */
  static bool headerValueIsValid(const absl::string_view header_value);

  /**
   * Validates that a header name is valid, according to RFC 7230, section 3.2.
   * http://tools.ietf.org/html/rfc7230#section-3.2
   * @return bool true if the header name is valid, according to the aforementioned RFC.
   */
  static bool headerNameIsValid(absl::string_view header_key);

  /**
   * Checks if header name contains underscore characters.
   * Underscore character is allowed in header names by the RFC-7230 and this check is implemented
   * as a security measure due to systems that treat '_' and '-' as interchangeable. Envoy by
   * default allows headers with underscore characters.
   * @return bool true if header name contains underscore characters.
   */
  static bool headerNameContainsUnderscore(const absl::string_view header_name);

  /**
   * Validates that the characters in the authority are valid.
   * @return bool true if the header values are valid, false otherwise.
   */
  static bool authorityIsValid(const absl::string_view authority_value);

  /**
   * @brief return if the 1xx should be handled by the [encode|decode]1xx calls.
   */
  static bool isSpecial1xx(const ResponseHeaderMap& response_headers);

  /**
   * @brief a helper function to determine if the headers represent a CONNECT request.
   */
  static bool isConnect(const RequestHeaderMap& headers);

  /**
   * @brief a helper function to determine if the headers represent a CONNECT-UDP request.
   */
  static bool isConnectUdpRequest(const RequestHeaderMap& headers);

  /**
   * @brief a helper function to determine if the headers represent a CONNECT-UDP response.
   */
  static bool isConnectUdpResponse(const ResponseHeaderMap& headers);

  /**
   * @brief a helper function to determine if the headers represent an accepted CONNECT response.
   */
  static bool isConnectResponse(const RequestHeaderMap* request_headers,
                                const ResponseHeaderMap& response_headers);

  /**
   * @brief Rewrites the authority header field by parsing the path using the default CONNECT-UDP
   * URI template. Returns true if the parsing was successful, otherwise returns false.
   */
  static bool rewriteAuthorityForConnectUdp(RequestHeaderMap& headers);

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  /**
   * @brief Returns true if the Capsule-Protocol header field (RFC 9297) is set to true. If the
   * header field is included multiple times, returns false as per RFC 9297.
   */
  static bool isCapsuleProtocol(const RequestOrResponseHeaderMap& headers);
#endif

  static bool requestShouldHaveNoBody(const RequestHeaderMap& headers);

  /**
   * @brief a helper function to determine if the headers represent an envoy internal request
   */
  static bool isEnvoyInternalRequest(const RequestHeaderMap& headers);

  /**
   * Determines if request headers pass Envoy validity checks.
   * @param headers to validate
   * @return details of the error if an error is present, otherwise absl::nullopt
   */
  static absl::optional<std::reference_wrapper<const absl::string_view>>
  requestHeadersValid(const RequestHeaderMap& headers);

  /**
   * Determines if the response should be framed by Connection: Close based on protocol
   * and headers.
   * @param protocol the protocol of the request
   * @param headers the request or response headers
   * @return if the response should be framed by Connection: Close
   */
  static bool shouldCloseConnection(Http::Protocol protocol,
                                    const RequestOrResponseHeaderMap& headers);

  /**
   * @brief Remove the trailing host dot from host/authority header.
   */
  static void stripTrailingHostDot(RequestHeaderMap& headers);

  /**
   * @return bool true if the provided host has a port, false otherwise.
   */
  static bool hostHasPort(absl::string_view host);

  /**
   * @brief Remove the port part from host/authority header if it is equal to provided port.
   * @return absl::optional<uint32_t> containing the port, if removed, else absl::nullopt.
   * If port is not passed, port part from host/authority header is removed.
   */
  static absl::optional<uint32_t> stripPortFromHost(RequestHeaderMap& headers,
                                                    absl::optional<uint32_t> listener_port);

  /**
   * @brief Remove the port part from host if it exists.
   */
  static void stripPortFromHost(std::string& host);

  /**
   * @brief Return the index of the port, or npos if the host has no port
   *
   * Note this does not do validity checks on the port, it just finds the
   * trailing : which is not a part of an IP address.
   */
  static absl::string_view::size_type getPortStart(absl::string_view host);

  /* Does a common header check ensuring required request headers are present.
   * Required request headers include :method header, :path for non-CONNECT requests, and
   * host/authority for HTTP/1.1 or CONNECT requests.
   * @return Status containing the result. If failed, message includes details on which header was
   * missing.
   */
  static Http::Status checkRequiredRequestHeaders(const Http::RequestHeaderMap& headers);

  /* Does a common header check ensuring required response headers are present.
   * Current required response headers only includes :status.
   * @return Status containing the result. If failed, message includes details on which header was
   * missing.
   */
  static Http::Status checkRequiredResponseHeaders(const Http::ResponseHeaderMap& headers);

  /* Does a common header check ensuring that header keys and values are valid and do not contain
   * forbidden characters (e.g. valid HTTP header keys/values should never contain embedded NULLs
   * or new lines.)
   * @return Status containing the result. If failed, message includes details on which header key
   * or value was invalid.
   */
  static Http::Status checkValidRequestHeaders(const Http::RequestHeaderMap& headers);

  /**
   * Returns true if a header may be safely removed without causing additional
   * problems. Effectively, header names beginning with ":" and the "host" header
   * may not be removed.
   */
  static bool isRemovableHeader(absl::string_view header);

  /**
   * Returns true if a header may be safely modified without causing additional
   * problems. Currently header names beginning with ":" and the "host" header
   * may not be modified.
   */
  static bool isModifiableHeader(absl::string_view header);

  enum class HeaderValidationResult {
    ACCEPT = 0,
    DROP,
    REJECT,
  };

  /**
   * Check if the given header_name has underscore.
   * Return HeaderValidationResult and populate the given counters based on
   * headers_with_underscores_action.
   */
  static HeaderValidationResult checkHeaderNameForUnderscores(
      absl::string_view header_name,
      envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
          headers_with_underscores_action,
      HeaderValidatorStats& stats);

  /**
   * Check if header_value represents a valid value for HTTP content-length header.
   * Return HeaderValidationResult and populate content_length_output if the value is valid,
   * otherwise populate should_close_connection according to
   * override_stream_error_on_invalid_http_message.
   */
  static HeaderValidationResult
  validateContentLength(absl::string_view header_value,
                        bool override_stream_error_on_invalid_http_message,
                        bool& should_close_connection, size_t& content_length_output);

  /**
   * Parse a comma-separated header string to the individual tokens. Discard empty tokens
   * and whitespace. Return a vector of the comma-separated tokens.
   */
  static std::vector<absl::string_view> parseCommaDelimitedHeader(absl::string_view header_value);

  /**
   * Return the part of attribute before first ';'-sign. For example,
   * "foo;bar=1" would return "foo".
   */
  static absl::string_view getSemicolonDelimitedAttribute(absl::string_view value);

  /**
   * Return a new AcceptEncoding header string vector.
   */
  static std::string addEncodingToAcceptEncoding(absl::string_view accept_encoding_header,
                                                 absl::string_view encoding);

  /**
   * Return `true` if the request is a standard HTTP CONNECT.
   * HTTP/1 RFC: https://datatracker.ietf.org/doc/html/rfc9110#section-9.3.6
   * HTTP/2 RFC: https://datatracker.ietf.org/doc/html/rfc9113#section-8.5
   */
  static bool isStandardConnectRequest(const Http::RequestHeaderMap& headers);

  /**
   * Return `true` if the request is an extended HTTP/2 CONNECT.
   * according to https://datatracker.ietf.org/doc/html/rfc8441#section-4
   */
  static bool isExtendedH2ConnectRequest(const Http::RequestHeaderMap& headers);

  /**
   * Return true if the given header name is a pseudo header.
   */
  static bool isPseudoHeader(absl::string_view header_name);
};

} // namespace Http
} // namespace Envoy
