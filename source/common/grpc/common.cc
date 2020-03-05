#include "common/grpc/common.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/macros.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/protobuf/protobuf.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/match.h"

namespace Envoy {
namespace Grpc {

bool Common::hasGrpcContentType(const Http::RequestOrResponseHeaderMap& headers) {
  const Http::HeaderEntry* content_type = headers.ContentType();
  // Content type is gRPC if it is exactly "application/grpc" or starts with
  // "application/grpc+". Specifically, something like application/grpc-web is not gRPC.
  return content_type != nullptr &&
         absl::StartsWith(content_type->value().getStringView(),
                          Http::Headers::get().ContentTypeValues.Grpc) &&
         (content_type->value().size() == Http::Headers::get().ContentTypeValues.Grpc.size() ||
          content_type->value()
                  .getStringView()[Http::Headers::get().ContentTypeValues.Grpc.size()] == '+');
}

bool Common::isGrpcResponseHeader(const Http::ResponseHeaderMap& headers, bool end_stream) {
  if (end_stream) {
    // Trailers-only response, only grpc-status is required.
    return headers.GrpcStatus() != nullptr;
  }
  if (Http::Utility::getResponseStatus(headers) != enumToInt(Http::Code::OK)) {
    return false;
  }
  return hasGrpcContentType(headers);
}

absl::optional<Status::GrpcStatus>
Common::getGrpcStatus(const Http::ResponseHeaderOrTrailerMap& trailers, bool allow_user_defined) {
  const Http::HeaderEntry* grpc_status_header = trailers.GrpcStatus();
  uint64_t grpc_status_code;

  if (!grpc_status_header || grpc_status_header->value().empty()) {
    return absl::nullopt;
  }
  if (!absl::SimpleAtoi(grpc_status_header->value().getStringView(), &grpc_status_code) ||
      (grpc_status_code > Status::WellKnownGrpcStatus::MaximumKnown && !allow_user_defined)) {
    return {Status::WellKnownGrpcStatus::InvalidCode};
  }
  return {static_cast<Status::GrpcStatus>(grpc_status_code)};
}

absl::optional<Status::GrpcStatus> Common::getGrpcStatus(const Http::ResponseTrailerMap& trailers,
                                                         const Http::ResponseHeaderMap& headers,
                                                         const StreamInfo::StreamInfo& info,
                                                         bool allow_user_defined) {
  // The gRPC specification does not guarantee a gRPC status code will be returned from a gRPC
  // request. When it is returned, it will be in the response trailers. With that said, Envoy will
  // treat a trailers-only response as a headers-only response, so we have to check the following
  // in order:
  //   1. trailers gRPC status, if it exists.
  //   2. headers gRPC status, if it exists.
  //   3. Inferred from info HTTP status, if it exists.
  const std::array<absl::optional<Grpc::Status::GrpcStatus>, 3> optional_statuses = {{
      {Grpc::Common::getGrpcStatus(trailers, allow_user_defined)},
      {Grpc::Common::getGrpcStatus(headers, allow_user_defined)},
      {info.responseCode() ? absl::optional<Grpc::Status::GrpcStatus>(
                                 Grpc::Utility::httpToGrpcStatus(info.responseCode().value()))
                           : absl::nullopt},
  }};

  for (const auto& optional_status : optional_statuses) {
    if (optional_status.has_value()) {
      return optional_status;
    }
  }

  return absl::nullopt;
}

std::string Common::getGrpcMessage(const Http::ResponseHeaderOrTrailerMap& trailers) {
  const auto entry = trailers.GrpcMessage();
  return entry ? std::string(entry->value().getStringView()) : EMPTY_STRING;
}

absl::optional<google::rpc::Status>
Common::getGrpcStatusDetailsBin(const Http::HeaderMap& trailers) {
  const Http::HeaderEntry* details_header = trailers.get(Http::Headers::get().GrpcStatusDetailsBin);
  if (!details_header) {
    return absl::nullopt;
  }

  // Some implementations use non-padded base64 encoding for grpc-status-details-bin.
  auto decoded_value = Base64::decodeWithoutPadding(details_header->value().getStringView());
  if (decoded_value.empty()) {
    return absl::nullopt;
  }

  google::rpc::Status status;
  if (!status.ParseFromString(decoded_value)) {
    return absl::nullopt;
  }

  return {std::move(status)};
}

Buffer::InstancePtr Common::serializeToGrpcFrame(const Protobuf::Message& message) {
  // http://www.grpc.io/docs/guides/wire.html
  // Reserve enough space for the entire message and the 5 byte header.
  // NB: we do not use prependGrpcFrameHeader because that would add another BufferFragment and this
  // (using a single BufferFragment) is more efficient.
  Buffer::InstancePtr body(new Buffer::OwnedImpl());
  const uint32_t size = message.ByteSize();
  const uint32_t alloc_size = size + 5;
  Buffer::RawSlice iovec;
  body->reserve(alloc_size, &iovec, 1);
  ASSERT(iovec.len_ >= alloc_size);
  iovec.len_ = alloc_size;
  uint8_t* current = reinterpret_cast<uint8_t*>(iovec.mem_);
  *current++ = 0; // flags
  const uint32_t nsize = htonl(size);
  std::memcpy(current, reinterpret_cast<const void*>(&nsize), sizeof(uint32_t));
  current += sizeof(uint32_t);
  Protobuf::io::ArrayOutputStream stream(current, size, -1);
  Protobuf::io::CodedOutputStream codec_stream(&stream);
  message.SerializeWithCachedSizes(&codec_stream);
  body->commit(&iovec, 1);
  return body;
}

Buffer::InstancePtr Common::serializeMessage(const Protobuf::Message& message) {
  auto body = std::make_unique<Buffer::OwnedImpl>();
  const uint32_t size = message.ByteSize();
  Buffer::RawSlice iovec;
  body->reserve(size, &iovec, 1);
  ASSERT(iovec.len_ >= size);
  iovec.len_ = size;
  uint8_t* current = reinterpret_cast<uint8_t*>(iovec.mem_);
  Protobuf::io::ArrayOutputStream stream(current, size, -1);
  Protobuf::io::CodedOutputStream codec_stream(&stream);
  message.SerializeWithCachedSizes(&codec_stream);
  body->commit(&iovec, 1);
  return body;
}

std::chrono::milliseconds Common::getGrpcTimeout(const Http::RequestHeaderMap& request_headers) {
  std::chrono::milliseconds timeout(0);
  const Http::HeaderEntry* header_grpc_timeout_entry = request_headers.GrpcTimeout();
  if (header_grpc_timeout_entry) {
    uint64_t grpc_timeout;
    // TODO(dnoe): Migrate to pure string_view (#6580)
    std::string grpc_timeout_string(header_grpc_timeout_entry->value().getStringView());
    const char* unit = StringUtil::strtoull(grpc_timeout_string.c_str(), grpc_timeout);
    if (unit != nullptr && *unit != '\0') {
      switch (*unit) {
      case 'H':
        timeout = std::chrono::hours(grpc_timeout);
        break;
      case 'M':
        timeout = std::chrono::minutes(grpc_timeout);
        break;
      case 'S':
        timeout = std::chrono::seconds(grpc_timeout);
        break;
      case 'm':
        timeout = std::chrono::milliseconds(grpc_timeout);
        break;
      case 'u':
        timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::microseconds(grpc_timeout));
        if (timeout < std::chrono::microseconds(grpc_timeout)) {
          timeout++;
        }
        break;
      case 'n':
        timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::nanoseconds(grpc_timeout));
        if (timeout < std::chrono::nanoseconds(grpc_timeout)) {
          timeout++;
        }
        break;
      }
    }
  }
  return timeout;
}

void Common::toGrpcTimeout(const std::chrono::milliseconds& timeout,
                           Http::RequestHeaderMap& headers) {
  uint64_t time = timeout.count();
  static const char units[] = "mSMH";
  const char* unit = units; // start with milliseconds
  static constexpr size_t MAX_GRPC_TIMEOUT_VALUE = 99999999;
  if (time > MAX_GRPC_TIMEOUT_VALUE) {
    time /= 1000; // Convert from milliseconds to seconds
    unit++;
  }
  while (time > MAX_GRPC_TIMEOUT_VALUE) {
    if (*unit == 'H') {
      time = MAX_GRPC_TIMEOUT_VALUE; // No bigger unit available, clip to max 8 digit hours.
    } else {
      time /= 60; // Convert from seconds to minutes to hours
      unit++;
    }
  }
  headers.setGrpcTimeout(absl::StrCat(time, absl::string_view(unit, 1)));
}

Http::RequestMessagePtr
Common::prepareHeaders(const std::string& upstream_cluster, const std::string& service_full_name,
                       const std::string& method_name,
                       const absl::optional<std::chrono::milliseconds>& timeout) {
  Http::RequestMessagePtr message(new Http::RequestMessageImpl());
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
  message->headers().setPath(absl::StrCat("/", service_full_name, "/", method_name));
  message->headers().setHost(upstream_cluster);
  // According to https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md TE should appear
  // before Timeout and ContentType.
  message->headers().setReferenceTE(Http::Headers::get().TEValues.Trailers);
  if (timeout) {
    toGrpcTimeout(timeout.value(), message->headers());
  }
  message->headers().setReferenceContentType(Http::Headers::get().ContentTypeValues.Grpc);

  return message;
}

void Common::checkForHeaderOnlyError(Http::ResponseMessage& http_response) {
  // First check for grpc-status in headers. If it is here, we have an error.
  absl::optional<Status::GrpcStatus> grpc_status_code =
      Common::getGrpcStatus(http_response.headers());
  if (!grpc_status_code) {
    return;
  }

  if (grpc_status_code.value() == Status::WellKnownGrpcStatus::InvalidCode) {
    throw Exception(absl::optional<uint64_t>(), "bad grpc-status header");
  }

  throw Exception(grpc_status_code.value(), Common::getGrpcMessage(http_response.headers()));
}

void Common::validateResponse(Http::ResponseMessage& http_response) {
  if (Http::Utility::getResponseStatus(http_response.headers()) != enumToInt(Http::Code::OK)) {
    throw Exception(absl::optional<uint64_t>(), "non-200 response code");
  }

  checkForHeaderOnlyError(http_response);

  // Check for existence of trailers.
  if (!http_response.trailers()) {
    throw Exception(absl::optional<uint64_t>(), "no response trailers");
  }

  absl::optional<Status::GrpcStatus> grpc_status_code =
      Common::getGrpcStatus(*http_response.trailers());
  if (!grpc_status_code || grpc_status_code.value() < 0) {
    throw Exception(absl::optional<uint64_t>(), "bad grpc-status trailer");
  }

  if (grpc_status_code.value() != 0) {
    throw Exception(grpc_status_code.value(), Common::getGrpcMessage(*http_response.trailers()));
  }
}

const std::string& Common::typeUrlPrefix() {
  CONSTRUCT_ON_FIRST_USE(std::string, "type.googleapis.com");
}

std::string Common::typeUrl(const std::string& qualified_name) {
  return typeUrlPrefix() + "/" + qualified_name;
}

void Common::prependGrpcFrameHeader(Buffer::Instance& buffer) {
  std::array<char, 5> header;
  header[0] = 0; // flags
  const uint32_t nsize = htonl(buffer.length());
  std::memcpy(&header[1], reinterpret_cast<const void*>(&nsize), sizeof(uint32_t));
  buffer.prepend(absl::string_view(&header[0], 5));
}

bool Common::parseBufferInstance(Buffer::InstancePtr&& buffer, Protobuf::Message& proto) {
  Buffer::ZeroCopyInputStreamImpl stream(std::move(buffer));
  return proto.ParseFromZeroCopyStream(&stream);
}

} // namespace Grpc
} // namespace Envoy
