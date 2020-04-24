#include "common/http/status.h"

#include "common/common/assert.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Http {

namespace {

constexpr absl::string_view EnvoyPayloadUrl = "Envoy";

absl::string_view statusCodeToString(StatusCode code) {
  switch (code) {
  case StatusCode::Ok:
    return absl::OkStatus().ToString();
  case StatusCode::CodecProtocolError:
    return "CodecProtocolError";
  case StatusCode::BufferFloodError:
    return "BufferFloodError";
  case StatusCode::PrematureResponseError:
    return "PrematureResponseError";
  case StatusCode::CodecClientError:
    return "CodecClientError";
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

struct EnvoyStatusPayload {
  EnvoyStatusPayload(StatusCode status_code) : status_code_(status_code) {}
  const StatusCode status_code_;
};

struct PrematureResponsePayload : public EnvoyStatusPayload {
  PrematureResponsePayload(Http::Code http_code)
      : EnvoyStatusPayload(StatusCode::PrematureResponseError), http_code_(http_code) {}
  const Http::Code http_code_;
};

template <typename T> void storePayload(absl::Status& status, const T& payload) {
  status.SetPayload(
      EnvoyPayloadUrl,
      absl::Cord(absl::string_view(reinterpret_cast<const char*>(&payload), sizeof(payload))));
}

template <typename T = EnvoyStatusPayload> const T* getPayload(const absl::Status& status) {
  auto payload = status.GetPayload(EnvoyPayloadUrl);
  ASSERT(payload.has_value(), "Must have payload");
  auto data = payload.value().Flatten();
  ASSERT(data.length() >= sizeof(T), "Invalid payload length");
  return reinterpret_cast<const T*>(data.data());
}

} // namespace

std::string toString(const Status& status) {
  if (status.ok()) {
    return status.ToString();
  }
  std::string text;
  auto status_code = getStatusCode(status);
  if (status_code != StatusCode::PrematureResponseError) {
    absl::StrAppend(&text, statusCodeToString(status_code), ": ", status.message());
  } else {
    auto http_code = getPrematureResponseHttpCode(status);
    absl::StrAppend(&text, "PrematureResponseError: HTTP code: ", http_code, ": ",
                    status.message());
  }
  return text;
}

Status codecProtocolError(absl::string_view message) {
  absl::Status status(absl::StatusCode::kInternal, message);
  storePayload(status, EnvoyStatusPayload(StatusCode::CodecProtocolError));
  return status;
}

Status bufferFloodError(absl::string_view message) {
  absl::Status status(absl::StatusCode::kInternal, message);
  storePayload(status, EnvoyStatusPayload(StatusCode::BufferFloodError));
  return status;
}

Status prematureResponseError(absl::string_view message, Http::Code http_code) {
  absl::Status status(absl::StatusCode::kInternal, message);
  storePayload(status, PrematureResponsePayload(http_code));
  return status;
}

Status codecClientError(absl::string_view message) {
  absl::Status status(absl::StatusCode::kInternal, message);
  storePayload(status, EnvoyStatusPayload(StatusCode::CodecClientError));
  return status;
}

// Methods for checking and extracting error information
StatusCode getStatusCode(const Status& status) {
  return status.ok() ? StatusCode::Ok : getPayload(status)->status_code_;
}

bool isCodecProtocolError(const Status& status) {
  return getStatusCode(status) == StatusCode::CodecProtocolError;
}

bool isBufferFloodError(const Status& status) {
  return getStatusCode(status) == StatusCode::BufferFloodError;
}

bool isPrematureResponseError(const Status& status) {
  return getStatusCode(status) == StatusCode::PrematureResponseError;
}

Http::Code getPrematureResponseHttpCode(const Status& status) {
  const auto* payload = getPayload<PrematureResponsePayload>(status);
  ASSERT(payload->status_code_ == StatusCode::PrematureResponseError,
         "Must be PrematureResponseError");
  return payload->http_code_;
}

bool isCodecClientError(const Status& status) {
  return getStatusCode(status) == StatusCode::CodecClientError;
}

} // namespace Http
} // namespace Envoy
