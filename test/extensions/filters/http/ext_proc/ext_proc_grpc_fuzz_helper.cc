#include "test/extensions/filters/http/ext_proc/ext_proc_grpc_fuzz_helper.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "envoy/type/v3/http_status.pb.h"

#include "source/common/common/thread.h"

#include "test/common/http/common.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::service::ext_proc::v3::CommonResponse;
using envoy::service::ext_proc::v3::HeaderMutation;
using envoy::service::ext_proc::v3::ImmediateResponse;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;
using envoy::type::v3::StatusCode;

const StatusCode HttpStatusCodes[] = {
    StatusCode::Continue,
    StatusCode::OK,
    StatusCode::Created,
    StatusCode::Accepted,
    StatusCode::NonAuthoritativeInformation,
    StatusCode::NoContent,
    StatusCode::ResetContent,
    StatusCode::PartialContent,
    StatusCode::MultiStatus,
    StatusCode::AlreadyReported,
    StatusCode::IMUsed,
    StatusCode::MultipleChoices,
    StatusCode::MovedPermanently,
    StatusCode::Found,
    StatusCode::SeeOther,
    StatusCode::NotModified,
    StatusCode::UseProxy,
    StatusCode::TemporaryRedirect,
    StatusCode::PermanentRedirect,
    StatusCode::BadRequest,
    StatusCode::Unauthorized,
    StatusCode::PaymentRequired,
    StatusCode::Forbidden,
    StatusCode::NotFound,
    StatusCode::MethodNotAllowed,
    StatusCode::NotAcceptable,
    StatusCode::ProxyAuthenticationRequired,
    StatusCode::RequestTimeout,
    StatusCode::Conflict,
    StatusCode::Gone,
    StatusCode::LengthRequired,
    StatusCode::PreconditionFailed,
    StatusCode::PayloadTooLarge,
    StatusCode::URITooLong,
    StatusCode::UnsupportedMediaType,
    StatusCode::RangeNotSatisfiable,
    StatusCode::ExpectationFailed,
    StatusCode::MisdirectedRequest,
    StatusCode::UnprocessableEntity,
    StatusCode::Locked,
    StatusCode::FailedDependency,
    StatusCode::UpgradeRequired,
    StatusCode::PreconditionRequired,
    StatusCode::TooManyRequests,
    StatusCode::RequestHeaderFieldsTooLarge,
    StatusCode::InternalServerError,
    StatusCode::NotImplemented,
    StatusCode::BadGateway,
    StatusCode::ServiceUnavailable,
    StatusCode::GatewayTimeout,
    StatusCode::HTTPVersionNotSupported,
    StatusCode::VariantAlsoNegotiates,
    StatusCode::InsufficientStorage,
    StatusCode::LoopDetected,
    StatusCode::NotExtended,
    StatusCode::NetworkAuthenticationRequired,
};

const grpc::StatusCode GrpcStatusCodes[] = {
    grpc::StatusCode::OK,
    grpc::StatusCode::CANCELLED,
    grpc::StatusCode::UNKNOWN,
    grpc::StatusCode::INVALID_ARGUMENT,
    grpc::StatusCode::DEADLINE_EXCEEDED,
    grpc::StatusCode::NOT_FOUND,
    grpc::StatusCode::ALREADY_EXISTS,
    grpc::StatusCode::PERMISSION_DENIED,
    grpc::StatusCode::RESOURCE_EXHAUSTED,
    grpc::StatusCode::FAILED_PRECONDITION,
    grpc::StatusCode::ABORTED,
    grpc::StatusCode::OUT_OF_RANGE,
    grpc::StatusCode::UNIMPLEMENTED,
    grpc::StatusCode::INTERNAL,
    grpc::StatusCode::UNAVAILABLE,
    grpc::StatusCode::DATA_LOSS,
    grpc::StatusCode::UNAUTHENTICATED,
};

ExtProcFuzzHelper::ExtProcFuzzHelper(FuzzedDataProvider* provider) { provider_ = provider; }

std::string ExtProcFuzzHelper::consumeRepeatedString() {
  const uint32_t str_len = provider_->ConsumeIntegralInRange<uint32_t>(0, ExtProcFuzzMaxDataSize);
  return {static_cast<char>(str_len), 'b'};
}

// Since FuzzedDataProvider requires enums to define a kMaxValue, we cannot
// use the envoy::type::v3::StatusCode enum directly.
StatusCode ExtProcFuzzHelper::randomHttpStatus() {
  const StatusCode rv = provider_->PickValueInArray(HttpStatusCodes);
  ENVOY_LOG_MISC(trace, "Selected HTTP StatusCode {}", rv);
  return rv;
}

// Since FuzzedDataProvider requires enums to define a kMaxValue, we cannot
// use the grpc::StatusCode enum directly.
grpc::StatusCode ExtProcFuzzHelper::randomGrpcStatusCode() {
  const grpc::StatusCode rv = provider_->PickValueInArray(GrpcStatusCodes);
  ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {}", rv);
  return rv;
}

void ExtProcFuzzHelper::logRequest(const ProcessingRequest* req) {
  if (req->has_request_headers()) {
    ENVOY_LOG_MISC(trace, "Received ProcessingRequest request_headers");
  } else if (req->has_response_headers()) {
    ENVOY_LOG_MISC(trace, "Received ProcessingRequest response_headers");
  } else if (req->has_request_body()) {
    ENVOY_LOG_MISC(trace, "Received ProcessingRequest request_body");
  } else if (req->has_response_body()) {
    ENVOY_LOG_MISC(trace, "Received ProcessingRequest response_body");
  } else if (req->has_request_trailers()) {
    ENVOY_LOG_MISC(trace, "Received ProcessingRequest request_trailers");
  } else if (req->has_response_trailers()) {
    ENVOY_LOG_MISC(trace, "Received ProcessingRequest response_trailers");
  } else {
    ENVOY_LOG_MISC(trace, "Received unexpected ProcessingRequest");
  }
}

grpc::Status ExtProcFuzzHelper::randomGrpcStatusWithMessage() {
  const grpc::StatusCode code = randomGrpcStatusCode();
  ENVOY_LOG_MISC(trace, "Closing stream with StatusCode {}", code);
  return {code, consumeRepeatedString()};
}

// TODO(ikepolinsky): implement this function
// Randomizes a HeaderMutation taken as input. Header/Trailer values of the
// request are available in req which allows for more guided manipulation of the
// headers. The bool value should be false to manipulate headers and
// true to manipulate trailers (which are also a header map)
void ExtProcFuzzHelper::randomizeHeaderMutation(HeaderMutation*, const ProcessingRequest*,
                                                const bool) {
  // Each of the following blocks generates random data for the 2 fields
  // of a HeaderMutation gRPC message

  // 1. Randomize set_headers
  // TODO(ikepolinsky): randomly add headers

  // 2. Randomize remove headers
  // TODO(ikepolinsky): Randomly remove headers
}

void ExtProcFuzzHelper::randomizeCommonResponse(CommonResponse* msg, const ProcessingRequest* req) {
  // Each of the following blocks generates random data for the 5 fields
  // of CommonResponse gRPC message
  // 1. Randomize status
  if (provider_->ConsumeBool()) {
    switch (provider_->ConsumeEnum<CommonResponseStatus>()) {
    case CommonResponseStatus::Continue:
      ENVOY_LOG_MISC(trace, "CommonResponse status CONTINUE");
      msg->set_status(CommonResponse::CONTINUE);
      break;
    case CommonResponseStatus::ContinueAndReplace:
      ENVOY_LOG_MISC(trace, "CommonResponse status CONTINUE_AND_REPLACE");
      msg->set_status(CommonResponse::CONTINUE_AND_REPLACE);
      break;
    default:
      RELEASE_ASSERT(false, "Unhandled case in random CommonResponse Status");
    }
  }

  // 2. Randomize header_mutation
  if (provider_->ConsumeBool()) {
    ENVOY_LOG_MISC(trace, "CommonResponse setting header_mutation");
    randomizeHeaderMutation(msg->mutable_header_mutation(), req, false);
  }

  // 3. Randomize body_mutation
  if (provider_->ConsumeBool()) {
    auto* body_mutation = msg->mutable_body_mutation();
    if (provider_->ConsumeBool()) {
      ENVOY_LOG_MISC(trace, "CommonResponse setting body_mutation, replacing body with bytes");
      body_mutation->set_body(consumeRepeatedString());
    } else {
      ENVOY_LOG_MISC(trace, "CommonResponse setting body_mutation, clearing body");
      body_mutation->set_clear_body(provider_->ConsumeBool());
    }
  }

  // 4. Randomize trailers
  // TODO(ikepolinsky) ext_proc currently does not support this field

  // 5. Randomize clear_route_cache
  if (provider_->ConsumeBool()) {
    ENVOY_LOG_MISC(trace, "CommonResponse clearing route cache");
    msg->set_clear_route_cache(true);
  }
}

void ExtProcFuzzHelper::randomizeImmediateResponse(ImmediateResponse* msg,
                                                   const ProcessingRequest* req) {
  // Each of the following blocks generates random data for the 5 fields
  // of an ImmediateResponse gRPC message
  // 1. Randomize HTTP status (required)
  ENVOY_LOG_MISC(trace, "ImmediateResponse setting status");
  msg->mutable_status()->set_code(randomHttpStatus());

  // 2. Randomize headers
  if (provider_->ConsumeBool()) {
    ENVOY_LOG_MISC(trace, "ImmediateResponse setting headers");
    randomizeHeaderMutation(msg->mutable_headers(), req, false);
  }

  // 3. Randomize body
  if (provider_->ConsumeBool()) {
    ENVOY_LOG_MISC(trace, "ImmediateResponse setting body");
    msg->set_body(consumeRepeatedString());
  }

  // 4. Randomize grpc_status
  if (provider_->ConsumeBool()) {
    ENVOY_LOG_MISC(trace, "ImmediateResponse setting grpc_status");
    msg->mutable_grpc_status()->set_status(randomGrpcStatusCode());
  }

  // 5. Randomize details
  if (provider_->ConsumeBool()) {
    ENVOY_LOG_MISC(trace, "ImmediateResponse setting details");
    msg->set_details(consumeRepeatedString());
  }
}

void ExtProcFuzzHelper::randomizeOverrideResponse(ProcessingMode* msg) {
  // Each of the following blocks generates random data for the 6 fields
  // of a ProcessingMode gRPC message
  // 1. Randomize request_header_mode
  if (provider_->ConsumeBool()) {
    switch (provider_->ConsumeEnum<HeaderSendSetting>()) {
    case HeaderSendSetting::Default:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting request_header_mode DEFAULT");
      msg->set_request_header_mode(ProcessingMode::DEFAULT);
      break;
    case HeaderSendSetting::Send:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting request_header_mode SEND");
      msg->set_request_header_mode(ProcessingMode::SEND);
      break;
    case HeaderSendSetting::Skip:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting request_header_mode SKIP");
      msg->set_request_header_mode(ProcessingMode::SKIP);
      break;
    default:
      RELEASE_ASSERT(false, "HeaderSendSetting not handled");
    }
  }

  // 2. Randomize response_header_mode
  if (provider_->ConsumeBool()) {
    switch (provider_->ConsumeEnum<HeaderSendSetting>()) {
    case HeaderSendSetting::Default:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting response_header_mode DEFAULT");
      msg->set_response_header_mode(ProcessingMode::DEFAULT);
      break;
    case HeaderSendSetting::Send:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting response_header_mode SEND");
      msg->set_response_header_mode(ProcessingMode::SEND);
      break;
    case HeaderSendSetting::Skip:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting response_header_mode SKIP");
      msg->set_response_header_mode(ProcessingMode::SKIP);
      break;
    default:
      RELEASE_ASSERT(false, "HeaderSendSetting not handled");
    }
  }

  // 3. Randomize request_body_mode
  if (provider_->ConsumeBool()) {
    switch (provider_->ConsumeEnum<BodySendSetting>()) {
    case BodySendSetting::None:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting request_body_mode NONE");
      msg->set_request_body_mode(ProcessingMode::NONE);
      break;
    case BodySendSetting::Streamed:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting request_body_mode STREAMED");
      msg->set_request_body_mode(ProcessingMode::STREAMED);
      break;
    case BodySendSetting::Buffered:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting request_body_mode BUFFERED");
      msg->set_request_body_mode(ProcessingMode::BUFFERED);
      break;
    case BodySendSetting::BufferedPartial:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting request_body_mode "
                            "BUFFERED_PARTIAL");
      msg->set_request_body_mode(ProcessingMode::BUFFERED_PARTIAL);
      break;
    default:
      RELEASE_ASSERT(false, "BodySendSetting not handled");
    }
  }

  // 4. Randomize response_body_mode
  if (provider_->ConsumeBool()) {
    switch (provider_->ConsumeEnum<BodySendSetting>()) {
    case BodySendSetting::None:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting response_body_mode NONE");
      msg->set_response_body_mode(ProcessingMode::NONE);
      break;
    case BodySendSetting::Streamed:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting response_body_mode STREAMED");
      msg->set_response_body_mode(ProcessingMode::STREAMED);
      break;
    case BodySendSetting::Buffered:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting response_body_mode BUFFERED");
      msg->set_response_body_mode(ProcessingMode::BUFFERED);
      break;
    case BodySendSetting::BufferedPartial:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting response_body_mode "
                            "BUFFERED_PARTIAL");
      msg->set_response_body_mode(ProcessingMode::BUFFERED_PARTIAL);
      break;
    default:
      RELEASE_ASSERT(false, "BodySendSetting not handled");
    }
  }

  // 5. Randomize request_trailer_mode
  if (provider_->ConsumeBool()) {
    switch (provider_->ConsumeEnum<HeaderSendSetting>()) {
    case HeaderSendSetting::Default:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting request_trailer_mode DEFAULT");
      msg->set_request_trailer_mode(ProcessingMode::DEFAULT);
      break;
    case HeaderSendSetting::Send:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting request_trailer_mode SEND");
      msg->set_request_trailer_mode(ProcessingMode::SEND);
      break;
    case HeaderSendSetting::Skip:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting request_trailer_mode SKIP");
      msg->set_request_trailer_mode(ProcessingMode::SKIP);
      break;
    default:
      RELEASE_ASSERT(false, "HeaderSendSetting not handled");
    }
  }

  // 6. Randomize response_trailer_mode
  if (provider_->ConsumeBool()) {
    switch (provider_->ConsumeEnum<HeaderSendSetting>()) {
    case HeaderSendSetting::Default:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting response_trailer_mode DEFAULT");
      msg->set_response_trailer_mode(ProcessingMode::DEFAULT);
      break;
    case HeaderSendSetting::Send:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting response_trailer_mode SEND");
      msg->set_response_trailer_mode(ProcessingMode::SEND);
      break;
    case HeaderSendSetting::Skip:
      ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting response_trailer_mode SKIP");
      msg->set_response_trailer_mode(ProcessingMode::SKIP);
      break;
    default:
      RELEASE_ASSERT(false, "HeaderSendSetting not handled");
    }
  }
}

void ExtProcFuzzHelper::randomizeResponse(ProcessingResponse* resp, const ProcessingRequest* req) {
  // Each of the following switch cases generate random data for 1 of the 7
  // ProcessingResponse.response fields
  switch (provider_->ConsumeEnum<ResponseType>()) {
  // 1. Randomize request_headers message
  case ResponseType::RequestHeaders: {
    ENVOY_LOG_MISC(trace, "ProcessingResponse setting request_headers response");
    CommonResponse* msg = resp->mutable_request_headers()->mutable_response();
    randomizeCommonResponse(msg, req);
    break;
  }
  // 2. Randomize response_headers message
  case ResponseType::ResponseHeaders: {
    ENVOY_LOG_MISC(trace, "ProcessingResponse setting response_headers response");
    CommonResponse* msg = resp->mutable_response_headers()->mutable_response();
    randomizeCommonResponse(msg, req);
    break;
  }
  // 3. Randomize request_body message
  case ResponseType::RequestBody: {
    ENVOY_LOG_MISC(trace, "ProcessingResponse setting request_body response");
    CommonResponse* msg = resp->mutable_request_body()->mutable_response();
    randomizeCommonResponse(msg, req);
    break;
  }
  // 4. Randomize response_body message
  case ResponseType::ResponseBody: {
    ENVOY_LOG_MISC(trace, "ProcessingResponse setting response_body response");
    CommonResponse* msg = resp->mutable_response_body()->mutable_response();
    randomizeCommonResponse(msg, req);
    break;
  }
  // 5. Randomize request_trailers message
  case ResponseType::RequestTrailers: {
    ENVOY_LOG_MISC(trace, "ProcessingResponse setting request_trailers response");
    HeaderMutation* header_mutation = resp->mutable_request_trailers()->mutable_header_mutation();
    randomizeHeaderMutation(header_mutation, req, true);
    break;
  }
  // 6. Randomize response_trailers message
  case ResponseType::ResponseTrailers: {
    ENVOY_LOG_MISC(trace, "ProcessingResponse setting response_trailers response");
    HeaderMutation* header_mutation = resp->mutable_response_trailers()->mutable_header_mutation();
    randomizeHeaderMutation(header_mutation, req, true);
    break;
  }
  // 7. Randomize immediate_response message
  case ResponseType::ImmediateResponse: {
    ENVOY_LOG_MISC(trace, "ProcessingResponse setting immediate_response response");
    ImmediateResponse* msg = resp->mutable_immediate_response();
    randomizeImmediateResponse(msg, req);
    break;
  }
  default:
    RELEASE_ASSERT(false, "ProcessingResponse Action not handled");
  }
}

grpc::Status ExtProcFuzzHelper::generateResponse(ProcessingRequest& req, ProcessingResponse& resp,
                                                 bool& immediate_close_grpc) {
  logRequest(&req);
  // The following blocks generate random data for the 9 fields of the
  // ProcessingResponse gRPC message

  // 1 - 7. Randomize response
  // If true, immediately close the connection with a random Grpc Status.
  // Otherwise randomize the response
  if (provider_->ConsumeBool()) {
    immediate_close_grpc = true;
    ENVOY_LOG_MISC(trace, "Immediately Closing gRPC connection");
    return randomGrpcStatusWithMessage();
  } else {
    ENVOY_LOG_MISC(trace, "Generating Random ProcessingResponse");
    randomizeResponse(&resp, &req);
  }

  // 8. Randomize dynamic_metadata
  // TODO(ikepolinsky): ext_proc does not support dynamic_metadata

  // 9. Randomize mode_override
  if (provider_->ConsumeBool()) {
    ENVOY_LOG_MISC(trace, "Generating Random ProcessingMode Override");
    ProcessingMode* msg = resp.mutable_mode_override();
    randomizeOverrideResponse(msg);
  }
  ENVOY_LOG_MISC(trace, "Response generated, writing to stream.");
  return grpc::Status::OK;
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
