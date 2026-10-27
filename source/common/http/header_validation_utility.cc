#include "source/common/http/header_validation_utility.h"

#include <string>

#include "envoy/http/header_validator_errors.h"

namespace Envoy {
namespace Http {
namespace HeaderValidationUtility {

#ifdef ENVOY_ENABLE_UHV

bool streamErrorOnlyErrors(absl::string_view error_details) {
  // Pre UHV HCM did not respect stream_error_on_invalid_http_message
  // and only sent 400 for specific errors.
  // TODO(#28555): make these errors respect the stream_error_on_invalid_http_message
  return error_details == UhvResponseCodeDetail::get().FragmentInUrlPath ||
         error_details == UhvResponseCodeDetail::get().EscapedSlashesInPath ||
         error_details == UhvResponseCodeDetail::get().Percent00InPath;
}

namespace {

// H/2 codec was resetting requests that were rejected due to headers with underscores,
// instead of sending 400. Preserving this behavior for now.
// TODO(#24466): Make H/2 behavior consistent with H/1 and H/3.
bool isHttp2UnderscoreReset(absl::string_view details, Protocol protocol) {
  return details == UhvResponseCodeDetail::get().InvalidUnderscore && protocol == Protocol::Http2;
}

} // namespace

std::optional<RequestRejection> checkRequestHeaders(ServerHeaderValidator& validator,
                                                    RequestHeaderMap& headers, Protocol protocol,
                                                    bool stream_error_on_invalid_http_message) {
  auto validation_result = validator.validateRequestHeaders(headers);
  bool failure = !validation_result.ok();
  bool redirect = false;
  bool transformation_failed = false;
  std::string failure_details(validation_result.details());
  if (!failure) {
    auto transformation_result = validator.transformRequestHeaders(headers);
    failure = !transformation_result.ok();
    transformation_failed = failure;
    redirect = transformation_result.action() ==
               ServerHeaderValidator::RequestHeadersTransformationResult::Action::Redirect;
    failure_details = std::string(transformation_result.details());
  }
  if (!failure) {
    return std::nullopt;
  }

  RequestRejection rejection;
  rejection.phase = RequestRejection::Phase::Headers;
  rejection.response_code =
      failure_details == Http1ResponseCodeDetail::get().InvalidTransferEncoding
          ? Code::NotImplemented
          : Code::BadRequest;
  rejection.transformation_failed = transformation_failed;
  if (isHttp2UnderscoreReset(failure_details, protocol)) {
    rejection.action = RequestRejection::Action::ResetStream;
    rejection.close_connection = false;
  } else {
    rejection.action =
        redirect ? RequestRejection::Action::Redirect : RequestRejection::Action::LocalReply;
    rejection.close_connection =
        !stream_error_on_invalid_http_message && !streamErrorOnlyErrors(failure_details);
  }
  rejection.details = std::move(failure_details);
  return rejection;
}

std::optional<RequestRejection> checkRequestTrailers(ServerHeaderValidator& validator,
                                                     RequestTrailerMap& trailers, Protocol protocol,
                                                     bool stream_error_on_invalid_http_message) {
  auto validation_result = validator.validateRequestTrailers(trailers);
  std::string failure_details(validation_result.details());
  if (validation_result.ok()) {
    auto transformation_result = validator.transformRequestTrailers(trailers);
    if (transformation_result.ok()) {
      return std::nullopt;
    }
    failure_details = std::string(transformation_result.details());
  }

  RequestRejection rejection;
  rejection.phase = RequestRejection::Phase::Trailers;
  rejection.response_code = Code::BadRequest;
  if (isHttp2UnderscoreReset(failure_details, protocol)) {
    rejection.action = RequestRejection::Action::ResetStream;
    rejection.close_connection = false;
  } else {
    // TODO(#24735): Harmonize H/2 and H/3 behavior with H/1
    rejection.action = protocol < Protocol::Http2 ? RequestRejection::Action::LocalReply
                                                  : RequestRejection::Action::ResetStream;
    rejection.close_connection = !stream_error_on_invalid_http_message;
  }
  rejection.details = std::move(failure_details);
  return rejection;
}

#endif // ENVOY_ENABLE_UHV

} // namespace HeaderValidationUtility
} // namespace Http
} // namespace Envoy
