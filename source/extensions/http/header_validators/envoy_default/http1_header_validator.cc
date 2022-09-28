#include "source/extensions/http/header_validators/envoy_default/http1_header_validator.h"

#include "source/extensions/http/header_validators/envoy_default/character_tables.h"

#include "absl/container/node_hash_set.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

using ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig;
using ::Envoy::Http::HeaderString;
using ::Envoy::Http::LowerCaseString;
using ::Envoy::Http::Protocol;
using ::Envoy::Http::RequestHeaderMap;
using HeaderValidatorFunction =
    HeaderValidator::HeaderValueValidationResult (Http1HeaderValidator::*)(const HeaderString&);

struct Http1ResponseCodeDetailValues {
  const std::string InvalidTransferEncoding = "uhv.http1.invalid_transfer_encoding";
  const std::string TransferEncodingNotAllowed = "uhv.http1.transfer_encoding_not_allowed";
  const std::string ContentLengthNotAllowed = "uhv.http1.content_length_not_allowed";
  const std::string ChunkedContentLength = "http1.content_length_and_chunked_not_allowed";
};

using Http1ResponseCodeDetail = ConstSingleton<Http1ResponseCodeDetailValues>;

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
                                           StreamInfo::StreamInfo& stream_info)
    : HeaderValidator(config, protocol, stream_info) {}

::Envoy::Http::HeaderValidator::HeaderEntryValidationResult
Http1HeaderValidator::validateRequestHeaderEntry(const HeaderString& key,
                                                 const HeaderString& value) {
  // Pseudo headers in HTTP/1.1 are synthesized by the codec from the request line prior to
  // submitting the header map for validation in UHV.
  static const absl::node_hash_map<absl::string_view, HeaderValidatorFunction> kHeaderValidatorMap{
      {":method", &Http1HeaderValidator::validateMethodHeader},
      {":authority", &Http1HeaderValidator::validateHostHeader},
      {":scheme", &Http1HeaderValidator::validateSchemeHeader},
      {":path", &Http1HeaderValidator::validatePathHeaderCharacters},
      {"transfer-encoding", &Http1HeaderValidator::validateTransferEncodingHeader},
      {"content-length", &Http1HeaderValidator::validateContentLengthHeader},
  };

  const auto& key_string_view = key.getStringView();
  if (key_string_view.empty()) {
    // reject empty header names
    return {HeaderEntryValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().EmptyHeaderName};
  }

  auto validator_it = kHeaderValidatorMap.find(key_string_view);
  if (validator_it != kHeaderValidatorMap.end()) {
    const auto& validator = validator_it->second;
    return (*this.*validator)(value);
  }

  if (key_string_view.at(0) != ':') {
    // Validate the (non-pseudo) header name
    auto name_result = validateGenericHeaderName(key);
    if (!name_result) {
      return name_result;
    }
  } else {
    // kHeaderValidatorMap contains every known pseudo header. If the header name starts with ":"
    // and we don't have a validator registered in the map, then the header name is an unknown
    // pseudo header.
    return {HeaderEntryValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidPseudoHeader};
  }

  return validateGenericHeaderValue(value);
}

::Envoy::Http::HeaderValidator::HeaderEntryValidationResult
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

::Envoy::Http::HeaderValidator::RequestHeaderMapValidationResult
Http1HeaderValidator::validateRequestHeaderMap(RequestHeaderMap& header_map) {
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
    return {RequestHeaderMapValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidMethod};
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
    return {RequestHeaderMapValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidHost};
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
  auto is_connect_method = header_map.method() == header_values_.MethodValues.Connect;
  auto is_options_method = header_map.method() == header_values_.MethodValues.Options;

  if (!is_connect_method && path.empty()) {
    // The :path is required for non-CONNECT requests.
    return {RequestHeaderMapValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidUrl};
  }

  auto path_is_asterisk = path == "*";
  auto path_is_absolute = path.empty() ? false : path.at(0) == '/';

  // HTTP/1.1 allows for a path of "*" when for OPTIONS requests, based on RFC
  // 9112, https://www.rfc-editor.org/rfc/rfc9112.html#section-3.2.4:
  //
  // The asterisk-form of request-target is only used for a server-wide OPTIONS
  // request
  // ...
  // asterisk-form  = "*"
  if (!is_options_method && path_is_asterisk) {
    return {RequestHeaderMapValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidUrl};
  }

  // Step 2: Validate Transfer-Encoding and Content-Length headers.
  // HTTP/1.1 disallows a Transfer-Encoding and Content-Length headers,
  // https://www.rfc-editor.org/rfc/rfc9112.html#section-6.2:
  //
  // A sender MUST NOT send a Content-Length header field in any message that
  // contains a Transfer-Encoding header field.
  //
  // The http1_protocol_options.allow_chunked_length config setting can
  // override the RFC compliance to allow a Transfer-Encoding of "chunked" with
  // a Content-Length set. In this exception case, we remove the Content-Length
  // header.
  if (header_map.TransferEncoding()) {
    // CONNECT methods must not contain any content so reject the request if Transfer-Encoding or
    // Content-Length is provided, per RFC 9110,
    // https://www.rfc-editor.org/rfc/rfc9110.html#section-9.3.6:
    //
    // A CONNECT request message does not have content. The interpretation of data sent after the
    // header section of the CONNECT request message is specific to the version of HTTP in use.
    if (is_connect_method) {
      return {RequestHeaderMapValidationResult::Action::Reject,
              Http1ResponseCodeDetail::get().TransferEncodingNotAllowed};
    }

    if (header_map.ContentLength() &&
        hasChunkedTransferEncoding(header_map.TransferEncoding()->value())) {
      if (!config_.http1_protocol_options().allow_chunked_length()) {
        // Configuration does not allow chunked length, reject the request
        return {RequestHeaderMapValidationResult::Action::Reject,
                Http1ResponseCodeDetail::get().ChunkedContentLength};
      } else {
        // Allow a chunked transfer encoding and remove the content length.
        header_map.removeContentLength();
      }
    }
  } else if (header_map.ContentLength() && is_connect_method) {
    if (header_map.getContentLengthValue() == "0") {
      // Remove a 0 content length from a CONNECT request
      header_map.removeContentLength();
    } else {
      // A content length in a CONNECT request is malformed
      return {RequestHeaderMapValidationResult::Action::Reject,
              Http1ResponseCodeDetail::get().ContentLengthNotAllowed};
    }
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
    std::size_t port_delim = host.rfind(":");
    if (port_delim == absl::string_view::npos || port_delim == 0) {
      // The uri-host is missing the port
      return {RequestHeaderMapValidationResult::Action::Reject,
              UhvResponseCodeDetail::get().InvalidHost};
    }

    if (host.at(0) == '[' && host.at(port_delim - 1) != ']') {
      // This is an IPv6 address and we would expect to see the closing "]" bracket just prior to
      // the port delimiter.
      return {RequestHeaderMapValidationResult::Action::Reject,
              UhvResponseCodeDetail::get().InvalidHost};
    }

    if (!path.empty()) {
      // CONNECT requests must not have a :path specified
      return {RequestHeaderMapValidationResult::Action::Reject,
              UhvResponseCodeDetail::get().InvalidUrl};
    }
  } else if (!config_.uri_path_normalization_options().skip_path_normalization() &&
             path_is_absolute) {
    // TODO(#6589) - Validate and normalize the path, which must be a valid URI. This will be
    // similar to:
    //
    // auto path_result = normalizePathUri(header_map);
    // if (path_result != RequestHeaderMapValidationResult::Accept) {
    //   return path_result;
    // }
  }

  // If path normalization is disabled or the path isn't absolute then the path will be validated
  // against the RFC character set in validateRequestHeaderEntry.

  // Step 4: Verify each request header
  std::string reject_details;
  std::vector<absl::string_view> drop_headers;
  header_map.iterate(
      [this, &reject_details, &drop_headers](
          const ::Envoy::Http::HeaderEntry& header_entry) -> ::Envoy::Http::HeaderMap::Iterate {
        const auto& header_name = header_entry.key();
        const auto& header_value = header_entry.value();
        const auto& string_header_name = header_name.getStringView();

        auto entry_result = validateRequestHeaderEntry(header_name, header_value);
        if (entry_result.action() == HeaderEntryValidationResult::Action::DropHeader) {
          // drop the header, continue processing the request
          drop_headers.push_back(string_header_name);
        } else if (!entry_result) {
          reject_details = static_cast<std::string>(entry_result.details());
        }

        return reject_details.empty() ? ::Envoy::Http::HeaderMap::Iterate::Continue
                                      : ::Envoy::Http::HeaderMap::Iterate::Break;
      });

  if (!reject_details.empty()) {
    return {RequestHeaderMapValidationResult::Action::Reject, reject_details};
  }

  for (auto& name : drop_headers) {
    header_map.remove(LowerCaseString(name));
  }

  return RequestHeaderMapValidationResult::success();
}

::Envoy::Http::HeaderValidator::ResponseHeaderMapValidationResult
Http1HeaderValidator::validateResponseHeaderMap(::Envoy::Http::ResponseHeaderMap& header_map) {
  // Step 1: verify that required pseudo headers are present
  //
  // For HTTP/1.1 responses, RFC 9112 states that only the :status
  // header is required, https://www.rfc-editor.org/rfc/rfc9112.html#section-4:
  //
  // status-line = HTTP-version SP status-code SP [ reason-phrase ] CRLF
  // status-code = 3DIGIT
  const auto status = header_map.getStatusValue();
  if (status.empty()) {
    return {ResponseHeaderMapValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidStatus};
  }

  // Step 2: Validate Transfer-Encoding
  const auto transfer_encoding = header_map.getTransferEncodingValue();
  if (!transfer_encoding.empty() && (status[0] == '1' || status == "204")) {
    // From RFC 9112, https://www.rfc-editor.org/rfc/rfc9112.html#section-6.1:
    //
    // A server MUST NOT send a Transfer-Encoding header field in any response with a status code
    // of 1xx (Informational) or 204 (No Content).
    return {ResponseHeaderMapValidationResult::Action::Reject,
            Http1ResponseCodeDetail::get().TransferEncodingNotAllowed};
  }

  // Step 3: Verify each response header
  std::string reject_details;
  std::vector<absl::string_view> drop_headers;
  header_map.iterate(
      [this, &reject_details, &drop_headers](
          const ::Envoy::Http::HeaderEntry& header_entry) -> ::Envoy::Http::HeaderMap::Iterate {
        const auto& header_name = header_entry.key();
        const auto& header_value = header_entry.value();
        const auto& string_header_name = header_name.getStringView();

        auto entry_result = validateResponseHeaderEntry(header_name, header_value);
        if (entry_result.action() == HeaderEntryValidationResult::Action::DropHeader) {
          // drop the header, continue processing the response
          drop_headers.push_back(string_header_name);
        } else if (!entry_result) {
          reject_details = static_cast<std::string>(entry_result.details());
        }

        return reject_details.empty() ? ::Envoy::Http::HeaderMap::Iterate::Continue
                                      : ::Envoy::Http::HeaderMap::Iterate::Break;
      });

  if (!reject_details.empty()) {
    return {ResponseHeaderMapValidationResult::Action::Reject, reject_details};
  }

  for (auto& name : drop_headers) {
    header_map.remove(LowerCaseString(name));
  }

  return ResponseHeaderMapValidationResult::success();
}

HeaderValidator::HeaderValueValidationResult
Http1HeaderValidator::validateTransferEncodingHeader(const HeaderString& value) {
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

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
