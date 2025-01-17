#include "source/extensions/http/header_validators/envoy_default/http1_header_validator.h"

#include "envoy/http/header_validator_errors.h"

#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/extensions/http/header_validators/envoy_default/character_tables.h"

#include "absl/container/node_hash_set.h"
#include "absl/functional/bind_front.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

using ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig;
using ::Envoy::Http::HeaderString;
using ::Envoy::Http::Protocol;
using ::Envoy::Http::RequestHeaderMap;
using ::Envoy::Http::UhvResponseCodeDetail;
using ValidationResult = ::Envoy::Http::HeaderValidator::ValidationResult;
using Http1ResponseCodeDetail = ::Envoy::Http::Http1ResponseCodeDetail;

/*
 * Header validation implementation for the Http/1 codec. This class follows guidance from
 * several RFCS:
 *
 * RFC 3986 <https://datatracker.ietf.org/doc/html/rfc3986> URI Generic Syntax
 * RFC 9110 <https://www.rfc-editor.org/rfc/rfc9110.html> HTTP Semantics
 * RFC 9112 <https://www.rfc-editor.org/rfc/rfc9112.html> HTTP/1.1
 *
 */
Http1HeaderValidator::Http1HeaderValidator(const HeaderValidatorConfig& config, Protocol protocol,
                                           ::Envoy::Http::HeaderValidatorStats& stats,
                                           const ConfigOverrides& config_overrides)
    : HeaderValidator(config, protocol, stats, config_overrides),
      request_header_validator_map_{
          {":method", absl::bind_front(&HeaderValidator::validateMethodHeader, this)},
          {":authority", absl::bind_front(&HeaderValidator::validateHostHeader, this)},
          {":scheme", absl::bind_front(&HeaderValidator::validateSchemeHeader, this)},
          {":path", getPathValidationMethod()},
          {"transfer-encoding",
           absl::bind_front(&Http1HeaderValidator::validateTransferEncodingHeader, this)},
          {"content-length", absl::bind_front(&HeaderValidator::validateContentLengthHeader, this)},
      } {}

HeaderValidator::HeaderValidatorFunction Http1HeaderValidator::getPathValidationMethod() {
  if (config_overrides_.allow_non_compliant_characters_in_path_) {
    return absl::bind_front(&Http1HeaderValidator::validatePathHeaderWithAdditionalCharacters,
                            this);
  }
  return absl::bind_front(&HeaderValidator::validatePathHeaderCharacters, this);
}

HeaderValidator::HeaderValueValidationResult
Http1HeaderValidator::validatePathHeaderWithAdditionalCharacters(
    const HeaderString& path_header_value) {
  ASSERT(config_overrides_.allow_non_compliant_characters_in_path_);
  // Same table as the kPathHeaderCharTable but with the following additional character allowed
  // " < > [ ] ^ ` { } \ |
  // This table is used when the "envoy.uhv.allow_non_compliant_characters_in_path"
  // runtime value is set to "true".
  static constexpr std::array<uint32_t, 8> kPathHeaderCharTableWithAdditionalCharacters = {
      // control characters
      0b00000000000000000000000000000000,
      // !"#$%&'()*+,-./0123456789:;<=>?
      0b01101111111111111111111111111110,
      //@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
      0b11111111111111111111111111111111,
      //`abcdefghijklmnopqrstuvwxyz{|}~
      0b11111111111111111111111111111110,
      // extended ascii
      0b00000000000000000000000000000000,
      0b00000000000000000000000000000000,
      0b00000000000000000000000000000000,
      0b00000000000000000000000000000000,
  };

  // Same table as the kUriQueryAndFragmentCharTable but with the following additional character
  // allowed " < > [ ] ^ ` { } \ | # This table is used when the
  // "envoy.uhv.allow_non_compliant_characters_in_path" runtime value is set to "true".
  static constexpr std::array<uint32_t, 8> kQueryAndFragmentCharTableWithAdditionalCharacters = {
      // control characters
      0b00000000000000000000000000000000,
      // !"#$%&'()*+,-./0123456789:;<=>?
      0b01111111111111111111111111111111,
      //@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
      0b11111111111111111111111111111111,
      //`abcdefghijklmnopqrstuvwxyz{|}~
      0b11111111111111111111111111111110,
      // extended ascii
      0b00000000000000000000000000000000,
      0b00000000000000000000000000000000,
      0b00000000000000000000000000000000,
      0b00000000000000000000000000000000,
  };
  return HeaderValidator::validatePathHeaderCharacterSet(
      path_header_value, kPathHeaderCharTableWithAdditionalCharacters,
      kQueryAndFragmentCharTableWithAdditionalCharacters);
}

HeaderValidator::HeaderEntryValidationResult
Http1HeaderValidator::validateRequestHeaderEntry(const HeaderString& key,
                                                 const HeaderString& value) {
  // Pseudo headers in HTTP/1.1 are synthesized by the codec from the request line prior to
  // submitting the header map for validation in UHV.
  return validateGenericRequestHeaderEntry(key, value, request_header_validator_map_);
}

HeaderValidator::HeaderEntryValidationResult
Http1HeaderValidator::validateResponseHeaderEntry(const HeaderString& key,
                                                  const HeaderString& value) {
  const auto& key_string_view = key.getStringView();
  if (key_string_view.empty()) {
    // reject empty header names
    return {HeaderEntryValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().EmptyHeaderName};
  }

  if (key_string_view == ":status") {
    // Validate the :status header against the RFC valid range
    return validateStatusHeader(value);
  } else if (key_string_view == "content-length") {
    // Validate the Content-Length header
    return validateContentLengthHeader(value);
  } else if (key_string_view == "transfer-encoding") {
    // Validate the Transfer-Encoding header
    return validateTransferEncodingHeader(value);
  } else if (key_string_view.at(0) != ':') {
    // Validate the generic header name.
    auto name_result = validateGenericHeaderName(key);
    if (!name_result) {
      return name_result;
    }
  } else {
    // The only valid pseudo header for responses is :status. If the header name starts with ":"
    // and it's not ":status", then the header name is an unknown pseudo header.
    return {HeaderEntryValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidPseudoHeader};
  }

  // Validate the header value
  return validateGenericHeaderValue(value);
}

ValidationResult Http1HeaderValidator::validateContentLengthAndTransferEncoding(
    const ::Envoy::Http::RequestOrResponseHeaderMap& header_map) {
  /**
   * Validate Transfer-Encoding and Content-Length headers.
   * HTTP/1.1 disallows a Transfer-Encoding and Content-Length headers,
   * https://www.rfc-editor.org/rfc/rfc9112.html#section-6.2:
   *
   * A sender MUST NOT send a Content-Length header field in any message that
   * contains a Transfer-Encoding header field.
   *
   * The http1_protocol_options.allow_chunked_length config setting can
   * override the RFC compliance to allow a Transfer-Encoding of "chunked" with
   * a Content-Length set. In this exception case, we remove the Content-Length
   * header in the transform[Request/Response]Headers() method.
   */
  if (header_map.TransferEncoding() && header_map.ContentLength() &&
      hasChunkedTransferEncoding(header_map.TransferEncoding()->value()) &&
      !config_.http1_protocol_options().allow_chunked_length()) {
    // Configuration does not allow chunked encoding and content-length, reject the request
    return {ValidationResult::Action::Reject, Http1ResponseCodeDetail::get().ChunkedContentLength};
  }
  return ValidationResult::success();
}

ValidationResult Http1HeaderValidator::validateRequestHeaders(const RequestHeaderMap& header_map) {
  absl::string_view path = header_map.getPathValue();
  absl::string_view host = header_map.getHostValue();
  // Step 1: verify that required pseudo headers are present. HTTP/1.1 requests requires the
  // :method header based on RFC 9112
  // https://www.rfc-editor.org/rfc/rfc9112.html#section-3:
  //
  // request-line   = method SP request-target SP HTTP-version CRLF
  //
  // The request-target will be stored in :path except for CONNECT requests which store the
  // request-target in :authority. So we only check that :method is set initially.
  if (header_map.getMethodValue().empty()) {
    return {ValidationResult::Action::Reject, UhvResponseCodeDetail::get().InvalidMethod};
  }

  // HTTP/1.1 also requires the Host header,
  // https://www.rfc-editor.org/rfc/rfc9112.html#section-3.2:
  //
  // A client MUST send a Host header field in all HTTP/1.1 request messages.
  // ...
  // A client MUST send a Host header field in an HTTP/1.1 request even if the
  // request-target is in the absolute-form
  // ...
  // If the authority component is missing or undefined for the target URI, then a
  // client MUST send a Host header field with an empty field-value.
  if (host.empty()) {
    return {ValidationResult::Action::Reject, UhvResponseCodeDetail::get().InvalidHost};
  }

  // Verify that the path and Host/:authority header matches based on the method.
  // From RFC 9112, https://www.rfc-editor.org/rfc/rfc9112.html#section-3.2.2:
  //
  // When a proxy receives a request with an absolute-form of request-target, the
  // proxy MUST ignore the received Host header field (if any) and instead replace
  // it with the host information of the request-target. A proxy that forwards
  // such a request MUST generate a new Host field-value based on the received
  // request-target rather than forward the received Host field-value.
  // ...
  // If the target URI includes an authority component, then a client MUST send a
  // field-value for Host that is identical to that authority component,
  // excluding any userinfo subcomponent and its "@" delimiter (Section 2.7.1).
  //
  // TODO(#6589) - This needs to be implemented after we have path normalization so that we can
  // parse the :path form and compare the authority component of the path against the :authority
  // header.
  auto is_connect_method = ::Envoy::Http::HeaderUtility::isConnect(header_map);
  auto is_options_method = header_map.getMethodValue() == header_values_.MethodValues.Options;

  if (!is_connect_method && path.empty()) {
    // The :path is required for non-CONNECT requests.
    return {ValidationResult::Action::Reject, UhvResponseCodeDetail::get().InvalidUrl};
  }

  auto path_is_asterisk = path == "*";

  // HTTP/1.1 allows for a path of "*" when for OPTIONS requests, based on RFC
  // 9112, https://www.rfc-editor.org/rfc/rfc9112.html#section-3.2.4:
  //
  // The asterisk-form of request-target is only used for a server-wide OPTIONS
  // request
  // ...
  // asterisk-form  = "*"
  if (!is_options_method && path_is_asterisk) {
    return {ValidationResult::Action::Reject, UhvResponseCodeDetail::get().InvalidUrl};
  }

  // Step 2: Validate Transfer-Encoding and Content-Length headers.
  if (header_map.TransferEncoding()) {
    // CONNECT methods must not contain any content so reject the request if Transfer-Encoding or
    // Content-Length is provided, per RFC 9110,
    // https://www.rfc-editor.org/rfc/rfc9110.html#section-9.3.6:
    //
    // A CONNECT request message does not have content. The interpretation of data sent after the
    // header section of the CONNECT request message is specific to the version of HTTP in use.
    if (is_connect_method) {
      return {ValidationResult::Action::Reject,
              Http1ResponseCodeDetail::get().TransferEncodingNotAllowed};
    }

    ValidationResult result = validateContentLengthAndTransferEncoding(header_map);
    if (!result.ok()) {
      return result;
    }
  }

  if (header_map.ContentLength() && header_map.getContentLengthValue() != "0" &&
      is_connect_method) {
    // A content length in a CONNECT request is malformed
    return {ValidationResult::Action::Reject,
            Http1ResponseCodeDetail::get().ContentLengthNotAllowed};
  }

  // Step 3: Normalize and validate :path header
  if (is_connect_method) {
    // The :authority must be authority-form for CONNECT method requests. From RFC
    // 9112: https://www.rfc-editor.org/rfc/rfc9112.html#section-3.2.3:
    //
    // The "authority-form" of request-target is only used for CONNECT requests (Section 9.3.6 of
    // [HTTP]). It consists of only the uri-host and port number of the tunnel destination,
    // separated by a colon (":").
    //
    //    authority-form = uri-host ":" port
    //
    // When making a CONNECT request to establish a tunnel through one or more proxies, a client
    // MUST send only the host and port of the tunnel destination as the request-target. The client
    // obtains the host and port from the target URI's authority component, except that it sends
    // the scheme's default port if the target URI elides the port. For example, a CONNECT request
    // to "http://www.example.com" looks like the following:
    //
    //    CONNECT www.example.com:80 HTTP/1.1
    //    Host: www.example.com
    //
    // Also from RFC 9110, the CONNECT request-target must have a valid port number,
    // https://www.rfc-editor.org/rfc/rfc9110.html#section-9.3.6:
    //
    // A server MUST reject a CONNECT request that targets an empty or invalid port number,
    // typically by responding with a 400 (Bad Request) status code
    //
    // This is a lazy check to see that the port delimiter exists because the actual host and
    // port value will be validated later on. For a host in reg-name form the delimiter existence
    // check is sufficient. For IPv6, we need to verify that the port delimiter occurs *after* the
    // IPv6 address (following a "]" character).
    std::size_t port_delim = host.rfind(':');
    if (port_delim == absl::string_view::npos || port_delim == 0) {
      // The uri-host is missing the port
      return {ValidationResult::Action::Reject, UhvResponseCodeDetail::get().InvalidHost};
    }

    if (host.at(0) == '[' && host.at(port_delim - 1) != ']') {
      // This is an IPv6 address and we would expect to see the closing "]" bracket just prior to
      // the port delimiter.
      return {ValidationResult::Action::Reject, UhvResponseCodeDetail::get().InvalidHost};
    }

    if (!path.empty()) {
      // CONNECT requests must not have a :path specified
      return {ValidationResult::Action::Reject, UhvResponseCodeDetail::get().InvalidUrl};
    }
  }

  // Step 4: Verify each request header
  std::string reject_details;
  header_map.iterate([this, &reject_details](const ::Envoy::Http::HeaderEntry& header_entry)
                         -> ::Envoy::Http::HeaderMap::Iterate {
    const auto& header_name = header_entry.key();
    const auto& header_value = header_entry.value();

    auto entry_result = validateRequestHeaderEntry(header_name, header_value);
    if (!entry_result.ok()) {
      reject_details = static_cast<std::string>(entry_result.details());
    }

    return reject_details.empty() ? ::Envoy::Http::HeaderMap::Iterate::Continue
                                  : ::Envoy::Http::HeaderMap::Iterate::Break;
  });

  if (!reject_details.empty()) {
    return {ValidationResult::Action::Reject, reject_details};
  }

  return ValidationResult::success();
}

void Http1HeaderValidator::sanitizeContentLength(
    ::Envoy::Http::RequestOrResponseHeaderMap& header_map) {
  // The http1_protocol_options.allow_chunked_length config setting can
  // override the RFC compliance to allow a Transfer-Encoding of "chunked" with
  // a Content-Length set. In this exception case, we remove the Content-Length
  // header.
  if (header_map.TransferEncoding() && header_map.ContentLength() &&
      hasChunkedTransferEncoding(header_map.TransferEncoding()->value()) &&
      config_.http1_protocol_options().allow_chunked_length()) {
    // Allow a chunked transfer encoding and remove the content length.
    header_map.removeContentLength();
  }
}

void ServerHttp1HeaderValidator::sanitizeContentLength(
    ::Envoy::Http::RequestHeaderMap& header_map) {
  if (header_map.ContentLength() && header_map.getContentLengthValue() == "0" &&
      ::Envoy::Http::HeaderUtility::isConnect(header_map)) {
    // Remove a 0 content length from a CONNECT request
    header_map.removeContentLength();
  } else {
    Http1HeaderValidator::sanitizeContentLength(header_map);
  }
}

::Envoy::Http::ServerHeaderValidator::RequestHeadersTransformationResult
ServerHttp1HeaderValidator::transformRequestHeaders(::Envoy::Http::RequestHeaderMap& header_map) {
  sanitizeContentLength(header_map);
  sanitizeHeadersWithUnderscores(header_map);
  sanitizePathWithFragment(header_map);
  auto path_result = transformUrlPath(header_map);
  if (!path_result.ok()) {
    return path_result;
  }
  return ::Envoy::Http::ServerHeaderValidator::RequestHeadersTransformationResult::success();
}

::Envoy::Http::HeaderValidator::TransformationResult
ClientHttp1HeaderValidator::transformResponseHeaders(::Envoy::Http::ResponseHeaderMap& header_map) {
  sanitizeContentLength(header_map);
  return TransformationResult::success();
}

ValidationResult
Http1HeaderValidator::validateResponseHeaders(const ::Envoy::Http::ResponseHeaderMap& header_map) {
  // Step 1: verify that required pseudo headers are present
  //
  // For HTTP/1.1 responses, RFC 9112 states that only the :status
  // header is required, https://www.rfc-editor.org/rfc/rfc9112.html#section-4:
  //
  // status-line = HTTP-version SP status-code SP [ reason-phrase ] CRLF
  // status-code = 3DIGIT
  const auto status = header_map.getStatusValue();
  if (status.empty()) {
    return {ValidationResult::Action::Reject, UhvResponseCodeDetail::get().InvalidStatus};
  }

  // Step 2: Validate Transfer-Encoding
  const auto transfer_encoding = header_map.getTransferEncodingValue();
  if (!transfer_encoding.empty() && (status[0] == '1' || status == "204")) {
    // From RFC 9112, https://www.rfc-editor.org/rfc/rfc9112.html#section-6.1:
    //
    // A server MUST NOT send a Transfer-Encoding header field in any response with a status code
    // of 1xx (Informational) or 204 (No Content).
    return {ValidationResult::Action::Reject,
            Http1ResponseCodeDetail::get().TransferEncodingNotAllowed};
  }

  ValidationResult result = validateContentLengthAndTransferEncoding(header_map);
  if (!result.ok()) {
    return result;
  }

  // Step 3: Verify each response header
  std::string reject_details;
  header_map.iterate([this, &reject_details](const ::Envoy::Http::HeaderEntry& header_entry)
                         -> ::Envoy::Http::HeaderMap::Iterate {
    const auto& header_name = header_entry.key();
    const auto& header_value = header_entry.value();

    auto entry_result = validateResponseHeaderEntry(header_name, header_value);
    if (!entry_result.ok()) {
      reject_details = static_cast<std::string>(entry_result.details());
    }

    return entry_result.ok() ? ::Envoy::Http::HeaderMap::Iterate::Continue
                             : ::Envoy::Http::HeaderMap::Iterate::Break;
  });

  if (!reject_details.empty()) {
    return {ValidationResult::Action::Reject, reject_details};
  }

  return ValidationResult::success();
}

HeaderValidator::HeaderValueValidationResult
Http1HeaderValidator::validateTransferEncodingHeader(const HeaderString& value) const {
  // HTTP/1.1 states that requests with an unrecognized transfer encoding should
  // be rejected, from RFC 9112, https://www.rfc-editor.org/rfc/rfc9112.html#section-6.1:
  //
  // A server that receives a request message with a transfer coding it does not understand SHOULD
  // respond with 501 (Not Implemented).
  //
  // This method implements the existing (pre-UHV) Envoy behavior of only allowing a "chunked"
  // Transfer-Encoding.
  const auto encoding = value.getStringView();

  if (!encoding.empty() &&
      !absl::EqualsIgnoreCase(encoding, header_values_.TransferEncodingValues.Chunked)) {
    return {HeaderValueValidationResult::Action::Reject,
            Http1ResponseCodeDetail::get().InvalidTransferEncoding};
  }

  return HeaderValueValidationResult::success();
}

ValidationResult
Http1HeaderValidator::validateRequestTrailers(const ::Envoy::Http::RequestTrailerMap& trailer_map) {
  return validateTrailers(trailer_map);
}

::Envoy::Http::HeaderValidator::TransformationResult
ServerHttp1HeaderValidator::transformRequestTrailers(
    ::Envoy::Http::RequestTrailerMap& trailer_map) {
  sanitizeHeadersWithUnderscores(trailer_map);
  return ::Envoy::Http::HeaderValidator::TransformationResult::success();
}

ValidationResult Http1HeaderValidator::validateResponseTrailers(
    const ::Envoy::Http::ResponseTrailerMap& trailer_map) {
  return validateTrailers(trailer_map);
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
