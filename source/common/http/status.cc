#include "source/common/http/status.h"

#include "source/common/common/assert.h"
#include "source/common/common/safe_memcpy.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Http {

namespace {

constexpr absl::string_view EnvoyPayloadUrl = "Envoy";

absl::string_view statusCodeToString(StatusCode code) {
  switch (code) {
  case StatusCode::Ok:
    return "OK";
  case StatusCode::CodecProtocolError:
    return "CodecProtocolError";
  case StatusCode::BufferFloodError:
    return "BufferFloodError";
  case StatusCode::PrematureResponseError:
    return "PrematureResponseError";
  case StatusCode::CodecClientError:
    return "CodecClientError";
  case StatusCode::InboundFramesWithEmptyPayload:
    return "InboundFramesWithEmptyPayloadError";
  }
  return "";
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
  const size_t payload_size = sizeof(payload);
  char* buffer = new char[payload_size];
  safeMemcpyUnsafeDst(buffer, &payload);
  absl::Cord cord(absl::string_view(buffer, payload_size));
  cord.Flatten(); // Flatten ahead of time for easier access later.
  status.SetPayload(EnvoyPayloadUrl, std::move(cord));
  free(buffer);
}

template <typename T = EnvoyStatusPayload> const T& getPayload(const absl::Status& status) {
  // The only way to get a reference to the payload owned by the absl::Status is through the
  // ForEachPayload method. All other methods create a copy of the payload, which is not convenient
  // for peeking at the payload value.
  const T* payload = nullptr;
  status.ForEachPayload([&payload](absl::string_view url, const absl::Cord& cord) {
    if (url == EnvoyPayloadUrl) {
      ASSERT(!payload); // Status API guarantees to have one payload with given URL
      auto data = cord.TryFlat();
      ASSERT(data.has_value()); // EnvoyPayloadUrl cords are flattened ahead of time
      ASSERT(data.value().length() >= sizeof(T), "Invalid payload length");
      char* buffer = new char[sizeof(T)];
      memcpy(buffer, data.value().data(), sizeof(T)); // NOLINT(safe-memcpy)
      payload = reinterpret_cast<const T*>(buffer);
    }
  });
  ASSERT(payload);
  return *payload;
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

Status inboundFramesWithEmptyPayloadError() {
  absl::Status status(absl::StatusCode::kInternal,
                      "Too many consecutive frames with an empty payload");
  storePayload(status, EnvoyStatusPayload(StatusCode::InboundFramesWithEmptyPayload));
  return status;
}

// Methods for checking and extracting error information
StatusCode getStatusCode(const Status& status) {
  return status.ok() ? StatusCode::Ok : getPayload(status).status_code_;
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
  const auto& payload = getPayload<PrematureResponsePayload>(status);
  ASSERT(payload.status_code_ == StatusCode::PrematureResponseError,
         "Must be PrematureResponseError");
  return payload.http_code_;
}

bool isCodecClientError(const Status& status) {
  return getStatusCode(status) == StatusCode::CodecClientError;
}

bool isInboundFramesWithEmptyPayloadError(const Status& status) {
  return getStatusCode(status) == StatusCode::InboundFramesWithEmptyPayload;
}

} // namespace Http
} // namespace Envoy
