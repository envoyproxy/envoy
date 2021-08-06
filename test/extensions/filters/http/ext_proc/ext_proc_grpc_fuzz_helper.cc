#include "test/extensions/filters/http/ext_proc/ext_proc_grpc_fuzz_helper.h"

#include <mutex>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3alpha/ext_proc.pb.h"
#include "envoy/service/ext_proc/v3alpha/external_processor.pb.h"
#include "envoy/type/v3/http_status.pb.h"

#include "test/common/http/common.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode;
using envoy::service::ext_proc::v3alpha::BodyResponse;
using envoy::service::ext_proc::v3alpha::CommonResponse;
using envoy::service::ext_proc::v3alpha::HeadersResponse;
using envoy::service::ext_proc::v3alpha::HttpBody;
using envoy::service::ext_proc::v3alpha::HttpHeaders;
using envoy::service::ext_proc::v3alpha::HttpTrailers;
using envoy::service::ext_proc::v3alpha::ImmediateResponse;
using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;
using envoy::service::ext_proc::v3alpha::TrailersResponse;
using envoy::service::ext_proc::v3alpha::HeaderMutation;
using envoy::type::v3::StatusCode;
using envoy::config::core::v3::HeaderValueOption;
using envoy::config::core::v3::HeaderValue;

ExtProcFuzzHelper::ExtProcFuzzHelper(FuzzedDataProvider* provider) {
  provider_ = provider;
  immediate_resp_sent_ = false;
}

// Wrapper functions for FuzzedDataProvider to make them thread safe
bool ExtProcFuzzHelper::ConsumeBool() {
  std::unique_lock<std::mutex> lock(provider_lock_);
  return provider_->ConsumeBool();
}

std::string ExtProcFuzzHelper::ConsumeRandomLengthString() {
  std::unique_lock<std::mutex> lock(provider_lock_);
  return provider_->ConsumeRandomLengthString();
}

// TODO(ikepolinsky): should this function be put in a standard place?
// Since FuzzedDataProvider requires enums to define a kMaxValue, we cannot
// use the envoy::type::v3::StatusCode enum directly. Additionally this allows
// us to be more verbose in our logging for tracing.
StatusCode ExtProcFuzzHelper::RandomHttpStatus() {
  switch (ConsumeIntegralInRange<uint32_t>(0,55)) {
    case 0:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (Continue)", StatusCode::Continue);
      // TODO(ikepolinsky): switch this back after bug fix
      //return StatusCode::Continue;
      return StatusCode::OK;
    case 1:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (OK)", StatusCode::OK);
      return StatusCode::OK;
    case 2:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (Created)", StatusCode::Created);
      return StatusCode::Created;
    case 3:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (Accepted)", StatusCode::Accepted);
      return StatusCode::Accepted;
    case 4:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (NonAuthoritativeInformation)", StatusCode::NonAuthoritativeInformation);
      return StatusCode::NonAuthoritativeInformation;
    case 5:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (NoContent)", StatusCode::NoContent);
      return StatusCode::NoContent;
    case 6:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (ResetContent)", StatusCode::ResetContent);
      return StatusCode::ResetContent;
    case 7:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (PartialContent)", StatusCode::PartialContent);
      return StatusCode::PartialContent;
    case 8:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (MultiStatus)", StatusCode::MultiStatus);
      return StatusCode::MultiStatus;
    case 9:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (AlreadyReported)", StatusCode::AlreadyReported);
      return StatusCode::AlreadyReported;
    case 10:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (IMUsed)", StatusCode::IMUsed);
      return StatusCode::IMUsed;
    case 11:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (MultipleChoices)", StatusCode::MultipleChoices);
      return StatusCode::MultipleChoices;
    case 12:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (MovedPermanently)", StatusCode::MovedPermanently);
      return StatusCode::MovedPermanently;
    case 13:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (Found)", StatusCode::Found);
      return StatusCode::Found;
    case 14:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (SeeOther)", StatusCode::SeeOther);
      return StatusCode::SeeOther;
    case 15:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (NotModified)", StatusCode::NotModified);
      return StatusCode::NotModified;
    case 16:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (UseProxy)", StatusCode::UseProxy);
      return StatusCode::UseProxy;
    case 17:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (TemporaryRedirect)", StatusCode::TemporaryRedirect);
      return StatusCode::TemporaryRedirect;
    case 18:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (PermanentRedirect)", StatusCode::PermanentRedirect);
      return StatusCode::PermanentRedirect;
    case 19:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (BadRequest)", StatusCode::BadRequest);
      return StatusCode::BadRequest;
    case 20:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (Unauthorized)", StatusCode::Unauthorized);
      return StatusCode::Unauthorized;
    case 21:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (PaymentRequired)", StatusCode::PaymentRequired);
      return StatusCode::PaymentRequired;
    case 22:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (Forbidden)", StatusCode::Forbidden);
      return StatusCode::Forbidden;
    case 23:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (NotFound)", StatusCode::NotFound);
      return StatusCode::NotFound;
    case 24:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (MethodNotAllowed)", StatusCode::MethodNotAllowed);
      return StatusCode::MethodNotAllowed;
    case 25:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (NotAcceptable)", StatusCode::NotAcceptable);
      return StatusCode::NotAcceptable;
    case 26:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (ProxyAuthenticationRequired)", StatusCode::ProxyAuthenticationRequired);
      return StatusCode::ProxyAuthenticationRequired;
    case 27:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (RequestTimeout)", StatusCode::RequestTimeout);
      return StatusCode::RequestTimeout;
    case 28:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (Conflict)", StatusCode::Conflict);
      return StatusCode::Conflict;
    case 29:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (Gone)", StatusCode::Gone);
      return StatusCode::Gone;
    case 30:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (LengthRequired)", StatusCode::LengthRequired);
      return StatusCode::LengthRequired;
    case 31:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (PreconditionFailed)", StatusCode::PreconditionFailed);
      return StatusCode::PreconditionFailed;
    case 32:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (PayloadTooLarge)", StatusCode::PayloadTooLarge);
      return StatusCode::PayloadTooLarge;
    case 33:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (URITooLong)", StatusCode::URITooLong);
      return StatusCode::URITooLong;
    case 34:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (UnsupportedMediaType)", StatusCode::UnsupportedMediaType);
      return StatusCode::UnsupportedMediaType;
    case 35:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (RangeNotSatisfiable)", StatusCode::RangeNotSatisfiable);
      return StatusCode::RangeNotSatisfiable;
    case 36:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (ExpectationFailed)", StatusCode::ExpectationFailed);
      return StatusCode::ExpectationFailed;
    case 37:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (MisdirectedRequest)", StatusCode::MisdirectedRequest);
      return StatusCode::MisdirectedRequest;
    case 38:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (UnprocessableEntity)", StatusCode::UnprocessableEntity);
      return StatusCode::UnprocessableEntity;
    case 39:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (Locked)", StatusCode::Locked);
      return StatusCode::Locked;
    case 40:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (FailedDependency)", StatusCode::FailedDependency);
      return StatusCode::FailedDependency;
    case 41:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (UpgradeRequired)", StatusCode::UpgradeRequired);
      return StatusCode::UpgradeRequired;
    case 42:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (PreconditionRequired)", StatusCode::PreconditionRequired);
      return StatusCode::PreconditionRequired;
    case 43:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (TooManyRequests)", StatusCode::TooManyRequests);
      return StatusCode::TooManyRequests;
    case 44:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (RequestHeaderFieldsTooLarge)", StatusCode::RequestHeaderFieldsTooLarge);
      return StatusCode::RequestHeaderFieldsTooLarge;
    case 45:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (InternalServerError)", StatusCode::InternalServerError);
      return StatusCode::InternalServerError;
    case 46:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (NotImplemented)", StatusCode::NotImplemented);
      return StatusCode::NotImplemented;
    case 47:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (BadGateway)", StatusCode::BadGateway);
      return StatusCode::BadGateway;
    case 48:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (ServiceUnavailable)", StatusCode::ServiceUnavailable);
      return StatusCode::ServiceUnavailable;
    case 49:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (GatewayTimeout)", StatusCode::GatewayTimeout);
      return StatusCode::GatewayTimeout;
    case 50:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (HTTPVersionNotSupported)", StatusCode::HTTPVersionNotSupported);
      return StatusCode::HTTPVersionNotSupported;
    case 51:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (VariantAlsoNegotiates)", StatusCode::VariantAlsoNegotiates);
      return StatusCode::VariantAlsoNegotiates;
    case 52:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (InsufficientStorage)", StatusCode::InsufficientStorage);
      return StatusCode::InsufficientStorage;
    case 53:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (LoopDetected)", StatusCode::LoopDetected);
      return StatusCode::LoopDetected;
    case 54:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (NotExtended)", StatusCode::NotExtended);
      return StatusCode::NotExtended;
    case 55:
      ENVOY_LOG_MISC(trace, "Selected HTTP Status Code {} (NetworkAuthenticationRequired)", StatusCode::NetworkAuthenticationRequired);
      return StatusCode::NetworkAuthenticationRequired;
    default:
      ENVOY_LOG_MISC(error, "Unhandled HTTP Status Code");
      exit(EXIT_FAILURE);
  }
}

// TODO(ikepolinsky): should this function be put in a standard place?
// Since FuzzedDataProvider requires enums to define a kMaxValue, we cannot
// use the grpc::StatusCode enum directly. Additionally this allows us to be
// more verbose in our logging for tracing.
grpc::StatusCode ExtProcFuzzHelper::RandomGrpcStatusCode() {
  switch (ConsumeIntegralInRange<uint32_t>(0, 16)) {
    case 0:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (OK)", grpc::StatusCode::OK);
      return grpc::StatusCode::OK;
    case 1:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (CANCELLED)", grpc::StatusCode::CANCELLED);
      return grpc::StatusCode::CANCELLED;
    case 2:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (UNKNOWN)", grpc::StatusCode::UNKNOWN);
      return grpc::StatusCode::UNKNOWN;
    case 3:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (INVALID_ARGUMENT)", grpc::StatusCode::INVALID_ARGUMENT);
      return grpc::StatusCode::INVALID_ARGUMENT;
    case 4:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (DEADLINE_EXCEEDED)", grpc::StatusCode::DEADLINE_EXCEEDED);
      return grpc::StatusCode::DEADLINE_EXCEEDED;
    case 5:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (NOT_FOUND)", grpc::StatusCode::NOT_FOUND);
      return grpc::StatusCode::NOT_FOUND;
    case 6:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (ALREADY_EXISTS)", grpc::StatusCode::ALREADY_EXISTS);
      return grpc::StatusCode::ALREADY_EXISTS;
    case 7:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (PERMISSION_DENIED)", grpc::StatusCode::PERMISSION_DENIED);
      return grpc::StatusCode::PERMISSION_DENIED;
    case 8:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (RESOURCE_EXHAUSTED)", grpc::StatusCode::RESOURCE_EXHAUSTED);
      return grpc::StatusCode::RESOURCE_EXHAUSTED;
    case 9:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (FAILED_PRECONDITION)", grpc::StatusCode::FAILED_PRECONDITION);
      return grpc::StatusCode::FAILED_PRECONDITION;
    case 10:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (ABORTED)", grpc::StatusCode::ABORTED);
      return grpc::StatusCode::ABORTED;
    case 11:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (OUT_OF_RANGE)", grpc::StatusCode::OUT_OF_RANGE);
      return grpc::StatusCode::OUT_OF_RANGE;
    case 12:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (UNIMPLEMMENTED)", grpc::StatusCode::UNIMPLEMENTED);
      return grpc::StatusCode::UNIMPLEMENTED;
    case 13:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (INTERNAL)", grpc::StatusCode::INTERNAL);
      return grpc::StatusCode::INTERNAL;
    case 14:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (UNAVAILABLE)", grpc::StatusCode::UNAVAILABLE);
      return grpc::StatusCode::UNAVAILABLE;
    case 15:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode {} (DATA_LOSS)", grpc::StatusCode::DATA_LOSS);
      return grpc::StatusCode::DATA_LOSS;
    case 16:
      ENVOY_LOG_MISC(trace, "Selected gRPC StatusCode (UNAUTHENTICATED)", grpc::StatusCode::UNAUTHENTICATED);
      return grpc::StatusCode::UNAUTHENTICATED;
    default:
      ENVOY_LOG_MISC(trace, "Unhandled gRPC status code");
      exit(EXIT_FAILURE);
  }
}

grpc::Status ExtProcFuzzHelper::RandomGrpcStatusWithMessage() {
  grpc::StatusCode code = RandomGrpcStatusCode();
  ENVOY_LOG_MISC(trace, "Closing stream with StatusCode {}", code);
  return grpc::Status(code, ConsumeRandomLengthString());
}

// TODO(ikepolinsky): implement this function
// Randomizes a HeaderMutation taken as input. Header/Trailer values of the
// request are availble in req which allows for more guided manipulation of the
// headers. The bool value should be false to manipulate headers and
// true to manipulate trailers (which are also a headermap)
void ExtProcFuzzHelper::RandomizeHeaderMutation(HeaderMutation*, ProcessingRequest*, bool) {
  // Each of the following blocks generates random data for the 2 fields
  // of a HeaderMutation gRPC message

  // 1. Randomize set_headers
  /* TODO(ikepolinsky): randomly add headers
  if (ConsumeBool()) {
    HeaderValueOption* set_header = msg->add_set_headers();
    set_header->mutable_header()->set_key(ConsumeRandomLengthString());
    set_header->mutable_header()->set_value(ConsumeRandomLengthString());
    set_header->mutable_append()->set_value(ConsumeBool());
  }
  */

  // 2. Randomize remove headers
  /* TODO(ikepolinsky): Randomly remove headers
  if (ConsumeBool()) {
    msg->add_remove_headers(ConsumeRandomLenthString());
  }*/
}

void ExtProcFuzzHelper::RandomizeCommonResponse(CommonResponse* msg, ProcessingRequest* req) {
  // Each of the following blocks generates random data for the 5 fields
  // of CommonResponse gRPC message
  // 1. Randomize status
  if (ConsumeBool()) {
    switch (ConsumeIntegralInRange<uint32_t>(0, 1)) {
      case 0:
        ENVOY_LOG_MISC(trace, "CommonResponse status CONTINUE");
        msg->set_status(CommonResponse::CONTINUE);
        break;
      case 1:
        ENVOY_LOG_MISC(trace, "CommonResponse status CONTINUE_AND_REPLACE");
        msg->set_status(CommonResponse::CONTINUE_AND_REPLACE);
        break;
      default:
        ENVOY_LOG_MISC(trace, "Unhandled case in random CommonResponse Status");
        exit(EXIT_FAILURE);
    }
  }

  // 2. Randomize header_mutation
  if (ConsumeBool()) {
    ENVOY_LOG_MISC(trace, "CommonResponse setting header_mutation");
    RandomizeHeaderMutation(msg->mutable_header_mutation(), req, false);
  }

  // 3. Randomize body_mutation
  if (ConsumeBool()) {
    auto* body_mutation = msg->mutable_body_mutation();
    if (ConsumeBool()) {
      ENVOY_LOG_MISC(trace, "CommonResponse setting body_mutation, replacing body with bytes");
      body_mutation->set_body(ConsumeRandomLengthString());
    } else {
      ENVOY_LOG_MISC(trace, "CommonResponse setting body_mutation, clearing body");
      body_mutation->set_clear_body(ConsumeBool());
    }
  }

  // 4. TODO(ikepolinsky): Randomize trailers - [skipping because tailers not implemented]

  // 5. Randomize clear_route_cache
  if (ConsumeBool()) {
    ENVOY_LOG_MISC(trace, "CommonResponse clearing route cache");
    msg->set_clear_route_cache(true);
  }
}

void ExtProcFuzzHelper::RandomizeImmediateResponse(ImmediateResponse* msg, ProcessingRequest* req) {
  // Each of the following blocks generates random data for the 5 fields
  // of an ImmediateResponse gRPC message
  // 1. Randomize HTTP status (required)
  ENVOY_LOG_MISC(trace, "ImmediateResponse setting status");
  msg->mutable_status()->set_code(RandomHttpStatus());

  // 2. Randomize headers
  if (ConsumeBool()) {
    ENVOY_LOG_MISC(trace, "ImmediateResponse setting headers");
    RandomizeHeaderMutation(msg->mutable_headers(), req, false);
  }

  // 3. Randomize body
  if (ConsumeBool()) {
    ENVOY_LOG_MISC(trace, "ImmediateResponse setting body");
    msg->set_body(ConsumeRandomLengthString());
  }

  // 4. Randomize grpc_status
  if (ConsumeBool()) {
    ENVOY_LOG_MISC(trace, "ImmediateResponse setting grpc_status");
    msg->mutable_grpc_status()->set_status(RandomGrpcStatusCode());
  }

  // 5. Randomize details
  if (ConsumeBool()) {
    ENVOY_LOG_MISC(trace, "ImmediateResponse setting details");
    msg->set_details(ConsumeRandomLengthString());
  }
}

void ExtProcFuzzHelper::RandomizeOverrideResponse(ProcessingMode* msg) {
  // Each of the following blocks generates random data for the 6 fields
  // of a ProcessingMode gRPC message
  // 1. Randomize request_header_mode
  if (ConsumeBool()) {
    switch (ConsumeEnum<HeaderSendSetting>()) {
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
        ENVOY_LOG_MISC(error, "HeaderSendSetting not handled");
        exit(EXIT_FAILURE);
    }
  }

  // 2. Randomize response_header_mode
  if (ConsumeBool()) {
    switch (ConsumeEnum<HeaderSendSetting>()) {
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
        ENVOY_LOG_MISC(error, "HeaderSendSetting not handled");
        exit(EXIT_FAILURE);
    }
  }

  // 3. Randomize request_body_mode
  if (ConsumeBool()) {
    switch (ConsumeEnum<BodySendSetting>()) {
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
        ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting request_body_mode BUFFERED_PARTIAL");
        msg->set_request_body_mode(ProcessingMode::BUFFERED_PARTIAL);
        break;
      default:
        ENVOY_LOG_MISC(error, "BodySendSetting not handled");
        exit(EXIT_FAILURE);
    }
  }

  // 4. Randomize response_body_mode
  if (ConsumeBool()) {
    switch (ConsumeEnum<BodySendSetting>()) {
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
        ENVOY_LOG_MISC(trace, "Override ProcessingMode: setting response_body_mode BUFFERED_PARTIAL");
        msg->set_response_body_mode(ProcessingMode::BUFFERED_PARTIAL);
        break;
      default:
        ENVOY_LOG_MISC(error, "BodySendSetting not handled");
        exit(EXIT_FAILURE);
    }
  }

  // 5. Randomize request_trailer_mode
  if (ConsumeBool()) {
    switch (ConsumeEnum<HeaderSendSetting>()) {
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
        ENVOY_LOG_MISC(error, "HeaderSendSetting not handled");
        exit(EXIT_FAILURE);
    }
  }

  // 6. Randomize response_trailer_mode
  if (ConsumeBool()) {
    switch (ConsumeEnum<HeaderSendSetting>()) {
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
        ENVOY_LOG_MISC(error, "HeaderSendSetting not handled");
        exit(EXIT_FAILURE);
    }
  }
}

void ExtProcFuzzHelper::RandomizeResponse(ProcessingResponse* resp, ProcessingRequest* req) {
  // Each of the following switch cases generate random data for 1 of the 7
  // ProcessingResponse.response fields
  switch (ConsumeEnum<ResponseType>()) {
    // 1. Randomize request_headers message
    case ResponseType::RequestHeaders: {
      ENVOY_LOG_MISC(trace, "ProcessingResponse setting request_headers response");
      CommonResponse* msg = resp->mutable_request_headers()->mutable_response();
      RandomizeCommonResponse(msg, req);
      break;
    }
    // 2. Randomize response_headers message
    case ResponseType::ResponseHeaders: {
      ENVOY_LOG_MISC(trace, "ProcessingResponse setting response_headers response");
      CommonResponse* msg = resp->mutable_response_headers()->mutable_response();
      RandomizeCommonResponse(msg, req);
      break;
    }
    // 3. Randomize request_body message
    case ResponseType::RequestBody: {
      ENVOY_LOG_MISC(trace, "ProcessingResponse setting request_body response");
      CommonResponse* msg = resp->mutable_request_body()->mutable_response();
      RandomizeCommonResponse(msg, req);
      break;
    }
    // 4. Randomize response_body message
    case ResponseType::ResponseBody: {
      ENVOY_LOG_MISC(trace, "ProcessingResponse setting response_body response");
      CommonResponse* msg = resp->mutable_response_body()->mutable_response();
      RandomizeCommonResponse(msg, req);
      break;
    }
    // 5. Randomize request_trailers message
    case ResponseType::RequestTrailers: {
      ENVOY_LOG_MISC(trace, "ProcessingResponse setting request_trailers response");
      HeaderMutation* header_mutation = resp->mutable_request_trailers()->mutable_header_mutation();
      RandomizeHeaderMutation(header_mutation, req, true);
      break;
    }
    // 6. Randomize response_trailers message
    case ResponseType::ResponseTrailers: {
      ENVOY_LOG_MISC(trace, "ProcessingResponse setting response_trailers response");
      HeaderMutation* header_mutation = resp->mutable_request_trailers()->mutable_header_mutation();
      RandomizeHeaderMutation(header_mutation, req, true);
      break;
    }
    // 7. Randomize immediate_response message
    case ResponseType::ImmediateResponse: {
      ENVOY_LOG_MISC(trace, "ProcessingResponse setting immediate_response response");
      ImmediateResponse* msg = resp->mutable_immediate_response();
      RandomizeImmediateResponse(msg, req);

      // Since we are sending an immediate response, envoy will close the
      // mock connection with the downstream. As a result, the
      // codec_client_connection will be deleted and if the upstream is still
      // sending data chunks (e.g., streaming mode) it will cause a seg fault
      // Note: At this point provider_lock_ is not held so deadlock is not
      // possible
      std::unique_lock<std::mutex> lock(immediate_resp_lock_);
      immediate_resp_sent_ = true;
      lock.unlock();
      break;
    }
    default:
      ENVOY_LOG_MISC(error, "ProcessingResponse Action not handled");
      exit(EXIT_FAILURE);
  }
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
