#include "common/common/status.h"

#include "absl/strings/str_cat.h"

namespace Envoy {

namespace {

const char EnvoyPayloadUrl[] = {"Envoy"};

absl::string_view StatusCodeToString(StatusCode code) {
  switch (code) {
  default:
    RELEASE_ASSERT(false, "Unknown status code");
  case StatusCode::CodecProtocolError:
    return "CodecProtocolError";
  case StatusCode::BufferFloodError:
    return "BufferFloodError";
  case StatusCode::PrematureResponseError:
    return "PrematureResponseError";
  case StatusCode::CodecClientError:
    return "CodecClientError";
  }
}

absl::StatusCode ToAbslStatusCode(StatusCode code) {
  switch (code) {
  default:
    RELEASE_ASSERT(false, "Unknown status code");
  case StatusCode::Ok:
    return absl::StatusCode::kOk;
  case StatusCode::CodecProtocolError:
    return absl::StatusCode::kFailedPrecondition;
  case StatusCode::BufferFloodError:
    return absl::StatusCode::kResourceExhausted;
  case StatusCode::PrematureResponseError:
    return absl::StatusCode::kInternal;
  case StatusCode::CodecClientError:
    return absl::StatusCode::kUnimplemented;
  }
}

StatusCode ToEnvoyStatusCode(absl::StatusCode code) {
  switch (code) {
  default:
    RELEASE_ASSERT(false, "Unknown status code");
  case absl::StatusCode::kOk:
    return StatusCode::Ok;
  case absl::StatusCode::kFailedPrecondition:
    return StatusCode::CodecProtocolError;
  case absl::StatusCode::kResourceExhausted:
    return StatusCode::BufferFloodError;
  case absl::StatusCode::kInternal:
    return StatusCode::PrematureResponseError;
  case absl::StatusCode::kUnimplemented:
    return StatusCode::CodecClientError;
  }
}

} // namespace

std::string ToString(const Status& status) {
  if (status.ok()) {
    return status.ToString();
  }
  std::string text;
  if (!IsPrematureResponseError(status)) {
    absl::StrAppend(&text, StatusCodeToString(ToEnvoyStatusCode(status.code())), ": ",
                    status.message());
  } else {
    auto http_code = GetPrematureResponseHttpCode(status);
    absl::StrAppend(&text, "PrematureResponseError: HTTP code: ", http_code, ": ",
                    status.message());
  }
  return text;
}

Status CodecProtocolError(absl::string_view message) {
  return absl::Status(ToAbslStatusCode(StatusCode::CodecProtocolError), message);
}

Status BufferFloodError(absl::string_view message) {
  return absl::Status(ToAbslStatusCode(StatusCode::BufferFloodError), message);
}

Status PrematureResponseError(absl::string_view message, Http::Code http_code) {
  absl::Status status(ToAbslStatusCode(StatusCode::PrematureResponseError), message);
  status.SetPayload(
      EnvoyPayloadUrl,
      absl::Cord(absl::string_view(reinterpret_cast<const char*>(&http_code), sizeof(http_code))));
  return status;
}

Status CodecClientError(absl::string_view message) {
  return absl::Status(ToAbslStatusCode(StatusCode::CodecClientError), message);
}

// Methods for checking and extracting error information
StatusCode GetStatusCode(const Status& status) { return ToEnvoyStatusCode(status.code()); }

bool IsCodecProtocolError(const Status& status) {
  return ToEnvoyStatusCode(status.code()) == StatusCode::CodecProtocolError;
}

bool IsBufferFloodError(const Status& status) {
  return ToEnvoyStatusCode(status.code()) == StatusCode::BufferFloodError;
}

bool IsPrematureResponseError(const Status& status) {
  return ToEnvoyStatusCode(status.code()) == StatusCode::PrematureResponseError;
}

Http::Code GetPrematureResponseHttpCode(const Status& status) {
  RELEASE_ASSERT(IsPrematureResponseError(status), "Must be PrematureResponseError");
  auto payload = status.GetPayload(EnvoyPayloadUrl);
  RELEASE_ASSERT(payload.has_value(), "Must have payload");
  auto data = payload.value().Flatten();
  RELEASE_ASSERT(data.length() == sizeof(Http::Code), "Invalid payload length");
  return *reinterpret_cast<const Http::Code*>(data.data());
}

bool IsCodecClientError(const Status& status) {
  return ToEnvoyStatusCode(status.code()) == StatusCode::CodecClientError;
}

} // namespace Envoy
