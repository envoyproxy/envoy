#include "source/common/grpc/common.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/base64.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/fmt.h"
#include "source/common/common/macros.h"
#include "source/common/common/safe_memcpy.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/codec.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/match.h"

namespace Envoy {
namespace Grpc {

bool Common::hasGrpcContentType(const Http::RequestOrResponseHeaderMap& headers) {
  const absl::string_view content_type = headers.getContentTypeValue();
  // Content type is gRPC if it is exactly "application/grpc" or starts with
  // "application/grpc+". Specifically, something like application/grpc-web is not gRPC.
  return absl::StartsWith(content_type, Http::Headers::get().ContentTypeValues.Grpc) &&
         (content_type.size() == Http::Headers::get().ContentTypeValues.Grpc.size() ||
          content_type[Http::Headers::get().ContentTypeValues.Grpc.size()] == '+');
}

bool Common::hasConnectProtocolVersionHeader(const Http::RequestOrResponseHeaderMap& headers) {
  return !headers.get(Http::CustomHeaders::get().ConnectProtocolVersion).empty();
}

bool Common::hasConnectStreamingContentType(const Http::RequestOrResponseHeaderMap& headers) {
  // Consider the request a connect request if the content type starts with "application/connect+".
  static constexpr absl::string_view connect_prefix{"application/connect+"};
  const absl::string_view content_type = headers.getContentTypeValue();
  return absl::StartsWith(content_type, connect_prefix);
}

bool Common::hasProtobufContentType(const Http::RequestOrResponseHeaderMap& headers) {
  return headers.getContentTypeValue() == Http::Headers::get().ContentTypeValues.Protobuf;
}

bool Common::isGrpcRequestHeaders(const Http::RequestHeaderMap& headers) {
  if (!headers.Path()) {
    return false;
  }
  return hasGrpcContentType(headers);
}

bool Common::isConnectRequestHeaders(const Http::RequestHeaderMap& headers) {
  if (!headers.Path()) {
    return false;
  }
  return hasConnectProtocolVersionHeader(headers);
}

bool Common::isConnectStreamingRequestHeaders(const Http::RequestHeaderMap& headers) {
  if (!headers.Path()) {
    return false;
  }
  return hasConnectStreamingContentType(headers);
}

bool Common::isProtobufRequestHeaders(const Http::RequestHeaderMap& headers) {
  if (!headers.Path()) {
    return false;
  }
  return hasProtobufContentType(headers);
}

bool Common::isGrpcResponseHeaders(const Http::ResponseHeaderMap& headers, bool end_stream) {
  if (end_stream) {
    // Trailers-only response, only grpc-status is required.
    return headers.GrpcStatus() != nullptr;
  }
  if (Http::Utility::getResponseStatus(headers) != enumToInt(Http::Code::OK)) {
    return false;
  }
  return hasGrpcContentType(headers);
}

bool Common::isConnectStreamingResponseHeaders(const Http::ResponseHeaderMap& headers) {
  if (Http::Utility::getResponseStatus(headers) != enumToInt(Http::Code::OK)) {
    return false;
  }
  return hasConnectStreamingContentType(headers);
}

absl::optional<Status::GrpcStatus>
Common::getGrpcStatus(const Http::ResponseHeaderOrTrailerMap& trailers, bool allow_user_defined) {
  const absl::string_view grpc_status_header = trailers.getGrpcStatusValue();
  uint64_t grpc_status_code;

  if (grpc_status_header.empty()) {
    return absl::nullopt;
  }
  if (!absl::SimpleAtoi(grpc_status_header, &grpc_status_code) ||
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
  absl::optional<Grpc::Status::GrpcStatus> optional_status;
  optional_status = Grpc::Common::getGrpcStatus(trailers, allow_user_defined);
  if (optional_status.has_value()) {
    return optional_status;
  }
  optional_status = Grpc::Common::getGrpcStatus(headers, allow_user_defined);
  if (optional_status.has_value()) {
    return optional_status;
  }
  return info.responseCode() ? absl::optional<Grpc::Status::GrpcStatus>(
                                   Grpc::Utility::httpToGrpcStatus(info.responseCode().value()))
                             : absl::nullopt;
}

std::string Common::getGrpcMessage(const Http::ResponseHeaderOrTrailerMap& trailers) {
  const auto entry = trailers.GrpcMessage();
  return entry ? std::string(entry->value().getStringView()) : EMPTY_STRING;
}

absl::optional<google::rpc::Status>
Common::getGrpcStatusDetailsBin(const Http::HeaderMap& trailers) {
  const auto details_header = trailers.get(Http::Headers::get().GrpcStatusDetailsBin);
  if (details_header.empty()) {
    return absl::nullopt;
  }

  // Some implementations use non-padded base64 encoding for grpc-status-details-bin.
  // This is effectively a trusted header so using the first value is fine.
  auto decoded_value = Base64::decodeWithoutPadding(details_header[0]->value().getStringView());
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
  auto reservation = body->reserveSingleSlice(alloc_size);
  ASSERT(reservation.slice().len_ >= alloc_size);
  uint8_t* current = reinterpret_cast<uint8_t*>(reservation.slice().mem_);
  *current++ = 0; // flags
  const uint32_t nsize = htonl(size);
  safeMemcpyUnsafeDst(current, &nsize);
  current += sizeof(uint32_t);
  Protobuf::io::ArrayOutputStream stream(current, size, -1);
  Protobuf::io::CodedOutputStream codec_stream(&stream);
  message.SerializeWithCachedSizes(&codec_stream);
  reservation.commit(alloc_size);
  return body;
}

Buffer::InstancePtr Common::serializeMessage(const Protobuf::Message& message) {
  auto body = std::make_unique<Buffer::OwnedImpl>();
  const uint32_t size = message.ByteSize();
  auto reservation = body->reserveSingleSlice(size);
  ASSERT(reservation.slice().len_ >= size);
  uint8_t* current = reinterpret_cast<uint8_t*>(reservation.slice().mem_);
  Protobuf::io::ArrayOutputStream stream(current, size, -1);
  Protobuf::io::CodedOutputStream codec_stream(&stream);
  message.SerializeWithCachedSizes(&codec_stream);
  reservation.commit(size);
  return body;
}

absl::optional<std::chrono::milliseconds>
Common::getGrpcTimeout(const Http::RequestHeaderMap& request_headers) {
  const Http::HeaderEntry* header_grpc_timeout_entry = request_headers.GrpcTimeout();
  std::chrono::milliseconds timeout;
  if (header_grpc_timeout_entry) {
    int64_t grpc_timeout;
    absl::string_view timeout_entry = header_grpc_timeout_entry->value().getStringView();
    if (timeout_entry.empty()) {
      // Must be of the form TimeoutValue TimeoutUnit. See
      // https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#requests.
      return absl::nullopt;
    }
    // TimeoutValue must be a positive integer of at most 8 digits.
    if (absl::SimpleAtoi(timeout_entry.substr(0, timeout_entry.size() - 1), &grpc_timeout) &&
        grpc_timeout >= 0 && static_cast<uint64_t>(grpc_timeout) <= MAX_GRPC_TIMEOUT_VALUE) {
      const char unit = timeout_entry[timeout_entry.size() - 1];
      switch (unit) {
      case 'H':
        return std::chrono::hours(grpc_timeout);
      case 'M':
        return std::chrono::minutes(grpc_timeout);
      case 'S':
        return std::chrono::seconds(grpc_timeout);
      case 'm':
        return std::chrono::milliseconds(grpc_timeout);
        break;
      case 'u':
        timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::microseconds(grpc_timeout));
        if (timeout < std::chrono::microseconds(grpc_timeout)) {
          timeout++;
        }
        return timeout;
      case 'n':
        timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::nanoseconds(grpc_timeout));
        if (timeout < std::chrono::nanoseconds(grpc_timeout)) {
          timeout++;
        }
        return timeout;
      }
    }
  }
  return absl::nullopt;
}

void Common::toGrpcTimeout(const std::chrono::milliseconds& timeout,
                           Http::RequestHeaderMap& headers) {
  uint64_t time = timeout.count();
  static const char units[] = "mSMH";
  const char* unit = units; // start with milliseconds
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
Common::prepareHeaders(const std::string& host_name, const std::string& service_full_name,
                       const std::string& method_name,
                       const absl::optional<std::chrono::milliseconds>& timeout) {
  Http::RequestMessagePtr message(new Http::RequestMessageImpl());
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
  message->headers().setPath(absl::StrCat("/", service_full_name, "/", method_name));
  message->headers().setHost(host_name);
  // According to https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md TE should appear
  // before Timeout and ContentType.
  message->headers().setReferenceTE(Http::Headers::get().TEValues.Trailers);
  if (timeout) {
    toGrpcTimeout(timeout.value(), message->headers());
  }
  message->headers().setReferenceContentType(Http::Headers::get().ContentTypeValues.Grpc);

  return message;
}

const std::string& Common::typeUrlPrefix() {
  CONSTRUCT_ON_FIRST_USE(std::string, "type.googleapis.com");
}

std::string Common::typeUrl(const std::string& qualified_name) {
  return typeUrlPrefix() + "/" + qualified_name;
}

void Common::prependGrpcFrameHeader(Buffer::Instance& buffer) {
  std::array<char, 5> header;
  header[0] = GRPC_FH_DEFAULT; // flags
  const uint32_t nsize = htonl(buffer.length());
  safeMemcpyUnsafeDst(&header[1], &nsize);
  buffer.prepend(absl::string_view(&header[0], 5));
}

bool Common::parseBufferInstance(Buffer::InstancePtr&& buffer, Protobuf::Message& proto) {
  Buffer::ZeroCopyInputStreamImpl stream(std::move(buffer));
  return proto.ParseFromZeroCopyStream(&stream);
}

absl::optional<Common::RequestNames>
Common::resolveServiceAndMethod(const Http::HeaderEntry* path) {
  absl::optional<RequestNames> request_names;
  if (path == nullptr) {
    return request_names;
  }
  absl::string_view str = path->value().getStringView();
  str = str.substr(0, str.find('?'));
  const auto parts = StringUtil::splitToken(str, "/");
  if (parts.size() != 2) {
    return request_names;
  }
  request_names = RequestNames{parts[0], parts[1]};
  return request_names;
}

} // namespace Grpc
} // namespace Envoy
