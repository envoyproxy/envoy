#include "source/extensions/filters/http/connect_grpc_bridge/end_stream_response.h"

#include "source/common/json/json_sanitizer.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectGrpcBridge {

namespace {

using WellKnownGrpcStatus = Grpc::Status::WellKnownGrpcStatus;

constexpr absl::string_view JsonCloseQuoteBrace = "\"}";
constexpr absl::string_view JsonErrorTag = "\"error\":";
constexpr absl::string_view JsonErrorCodeTag = "{\"code\":\"";
constexpr absl::string_view JsonErrorMessageTag = "\",\"message\":\"";
constexpr absl::string_view JsonErrorDetailsTypeTag = "\",\"details\":[{\"type\":\"";
constexpr absl::string_view JsonErrorDetailNextTypeTag = "\"},{\"type\":\"";
constexpr absl::string_view JsonErrorDetailValueTag = "\",\"value\":\"";
constexpr absl::string_view JsonErrorDetailsClose = "\"}]}";
constexpr absl::string_view JsonMetadataTag = "\"metadata\":{";

/**
 * Returns the appropriate error status code for a given gRPC status.
 *
 * https://connect.build/docs/protocol#error-codes
 */
absl::string_view statusCodeToString(std::string& buffer, const Grpc::Status::GrpcStatus status) {
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
    buffer = fmt::format("code_{}", status);
    return buffer;
  }
}

} // namespace

/**
 * Serializes a Buf Connect Error object to JSON.
 *
 * https://connect.build/docs/protocol#error-end-stream
 */
void serializeJson(const Error& error, Buffer::Instance& buffer) {
  std::string tmp_1;

  buffer.addFragments({JsonErrorCodeTag, statusCodeToString(tmp_1, error.code)});

  if (!error.message.empty()) {
    buffer.addFragments({JsonErrorMessageTag, Json::sanitize(tmp_1, error.message)});
  }

  if (!error.details.empty()) {
    const auto& details = error.details;

    auto serializeDetail = [&buffer](absl::string_view prefix, const ErrorDetail& detail) {
      std::string tmp_1, tmp_2;
      buffer.addFragments({
          prefix,
          Json::sanitize(tmp_1, detail.type),
          JsonErrorDetailValueTag,
          Json::sanitize(tmp_2, detail.value),
      });
    };

    auto it = details.begin();
    serializeDetail(JsonErrorDetailsTypeTag, *it);
    for (; it != details.end(); it++) {
      serializeDetail(JsonErrorDetailNextTypeTag, *it);
    }
    buffer.add(JsonErrorDetailsClose);
  } else {
    buffer.add(JsonCloseQuoteBrace);
  }
}

/**
 * Serializes a Buf Connect EndStreamResponse object to JSON.
 *
 * https://connect.build/docs/protocol#error-end-stream
 */
void serializeJson(const EndStreamResponse& response, Buffer::Instance& buffer) {
  buffer.add("{");

  if (response.error) {
    buffer.add(JsonErrorTag);

    serializeJson(*response.error, buffer);

    if (!response.metadata.empty()) {
      buffer.add(",");
    }
  }

  if (!response.metadata.empty()) {
    const auto& metadata = response.metadata;
    buffer.add(JsonMetadataTag);

    auto serializeMeta = [&buffer](absl::string_view prefix, absl::string_view key,
                                   const std::vector<std::string>& values) {
      std::string tmp_1;
      buffer.addFragments({prefix, Json::sanitize(tmp_1, key), "\":["});

      if (!values.empty()) {
        auto it = values.begin();
        buffer.addFragments({"\"", Json::sanitize(tmp_1, *it), "\""});
        for (; it != values.end(); it++) {
          buffer.addFragments({",\"", Json::sanitize(tmp_1, *it), "\""});
        }
      }

      buffer.add("]");
    };

    auto it = metadata.begin();
    serializeMeta("\"", it->first, it->second);
    for (; it != metadata.end(); it++) {
      serializeMeta(",\"", it->first, it->second);
    }
  }

  buffer.add("}");
}

/**
 * Gets the appropriate HTTP status code for a given gRPC status.
 *
 * https://connect.build/docs/protocol#error-codes
 */
uint64_t statusCodeToConnectUnaryStatus(const Grpc::Status::GrpcStatus status) {
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
