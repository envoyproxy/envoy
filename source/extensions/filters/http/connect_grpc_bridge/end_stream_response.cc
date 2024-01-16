#include "source/extensions/filters/http/connect_grpc_bridge/end_stream_response.h"

#include "source/common/common/base64.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectGrpcBridge {

namespace {

/**
 * Returns the appropriate error status code for a given gRPC status.
 *
 * https://connectrpc.com/docs/protocol#error-codes
 */
std::string statusCodeToString(const Grpc::Status::GrpcStatus status) {
  using WellKnownGrpcStatus = Grpc::Status::WellKnownGrpcStatus;

  switch (status) {
  case WellKnownGrpcStatus::Canceled:
    return "canceled";
  case WellKnownGrpcStatus::Unknown:
    return "unknown";
  case WellKnownGrpcStatus::InvalidArgument:
    return "invalid_argument";
  case WellKnownGrpcStatus::DeadlineExceeded:
    return "deadline_exceeded";
  case WellKnownGrpcStatus::NotFound:
    return "not_found";
  case WellKnownGrpcStatus::AlreadyExists:
    return "already_exists";
  case WellKnownGrpcStatus::PermissionDenied:
    return "permission_denied";
  case WellKnownGrpcStatus::ResourceExhausted:
    return "resource_exhausted";
  case WellKnownGrpcStatus::FailedPrecondition:
    return "failed_precondition";
  case WellKnownGrpcStatus::Aborted:
    return "aborted";
  case WellKnownGrpcStatus::OutOfRange:
    return "out_of_range";
  case WellKnownGrpcStatus::Unimplemented:
    return "unimplemented";
  case WellKnownGrpcStatus::Internal:
    return "internal";
  case WellKnownGrpcStatus::Unavailable:
    return "unavailable";
  case WellKnownGrpcStatus::DataLoss:
    return "data_loss";
  case WellKnownGrpcStatus::Unauthenticated:
    return "unauthenticated";
  default:
    return fmt::format("code_{}", status);
  }
}

ProtobufWkt::Struct convertToStruct(const Error& error) {
  ProtobufWkt::Struct obj;
  (*obj.mutable_fields())["code"] = ValueUtil::stringValue(statusCodeToString(error.code));

  if (!error.message.empty()) {
    (*obj.mutable_fields())["message"] = ValueUtil::stringValue(error.message);
  }

  if (!error.details.empty()) {
    auto details_list = std::make_unique<ProtobufWkt::ListValue>();
    for (const auto& detail : error.details) {
      const auto& value = detail.value();
      *details_list->add_values() = ValueUtil::structValue(MessageUtil::keyValueStruct({
          {"type", detail.type_url()},
          {"value", Base64::encode(value.c_str(), value.size())},
      }));
    }

    ProtobufWkt::Value details_value;
    details_value.set_allocated_list_value(details_list.release());
    (*obj.mutable_fields())["details"] = details_value;
  }
  return obj;
}

ProtobufWkt::Struct convertToStruct(const EndStreamResponse& response) {
  ProtobufWkt::Struct obj;

  if (response.error.has_value()) {
    (*obj.mutable_fields())["error"] = ValueUtil::structValue(convertToStruct(*response.error));
  }

  if (!response.metadata.empty()) {
    ProtobufWkt::Struct metadata_obj;
    for (const auto& [name, values] : response.metadata) {
      auto values_list = std::make_unique<ProtobufWkt::ListValue>();

      for (const auto& value : values) {
        *values_list->add_values() = ValueUtil::stringValue(value);
      }

      ProtobufWkt::Value values_value;
      values_value.set_allocated_list_value(values_list.release());
      (*metadata_obj.mutable_fields())[name] = values_value;
    }
    (*obj.mutable_fields())["metadata"] = ValueUtil::structValue(metadata_obj);
  }

  return obj;
}

} // namespace

bool serializeJson(const Error& error, std::string& out) {
  ProtobufWkt::Struct message = convertToStruct(error);
  return Protobuf::util::MessageToJsonString(message, &out).ok();
}

bool serializeJson(const EndStreamResponse& response, std::string& out) {
  ProtobufWkt::Struct message = convertToStruct(response);
  return Protobuf::util::MessageToJsonString(message, &out).ok();
}

/**
 * Gets the appropriate HTTP status code for a given gRPC status.
 *
 * https://connectrpc.com/docs/protocol#error-codes
 */
uint64_t statusCodeToConnectUnaryStatus(const Grpc::Status::GrpcStatus status) {
  using WellKnownGrpcStatus = Grpc::Status::WellKnownGrpcStatus;

  switch (status) {
  case WellKnownGrpcStatus::Canceled:
  case WellKnownGrpcStatus::DeadlineExceeded:
    return 408;
  case WellKnownGrpcStatus::InvalidArgument:
  case WellKnownGrpcStatus::OutOfRange:
    return 400;
  case WellKnownGrpcStatus::NotFound:
  case WellKnownGrpcStatus::Unimplemented:
    return 404;
  case WellKnownGrpcStatus::AlreadyExists:
  case WellKnownGrpcStatus::Aborted:
    return 409;
  case WellKnownGrpcStatus::PermissionDenied:
    return 403;
  case WellKnownGrpcStatus::ResourceExhausted:
    return 429;
  case WellKnownGrpcStatus::FailedPrecondition:
    return 412;
  case WellKnownGrpcStatus::Unavailable:
    return 503;
  case WellKnownGrpcStatus::Unauthenticated:
    return 401;
  default:
    return 500;
  }
}

} // namespace ConnectGrpcBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
