#include "common/grpc/common.h"

#include <arpa/inet.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Grpc {

const std::string Common::GRPC_CONTENT_TYPE{"application/grpc"};

void Common::chargeStat(const Upstream::ClusterInfo& cluster, const std::string& protocol,
                        const std::string& grpc_service, const std::string& grpc_method,
                        const Http::HeaderEntry* grpc_status) {
  if (!grpc_status) {
    return;
  }
  uint64_t grpc_status_code;
  const bool success =
      StringUtil::atoul(grpc_status->value().c_str(), grpc_status_code) && grpc_status_code == 0;
  chargeStat(cluster, protocol, grpc_service, grpc_method, success);
}

void Common::chargeStat(const Upstream::ClusterInfo& cluster, const std::string& protocol,
                        const std::string& grpc_service, const std::string& grpc_method,
                        bool success) {
  cluster.statsScope()
      .counter(fmt::format("{}.{}.{}.{}", protocol, grpc_service, grpc_method,
                           success ? "success" : "failure"))
      .inc();
  cluster.statsScope()
      .counter(fmt::format("{}.{}.{}.total", protocol, grpc_service, grpc_method))
      .inc();
}

void Common::chargeStat(const Upstream::ClusterInfo& cluster, const std::string& grpc_service,
                        const std::string& grpc_method, bool success) {
  chargeStat(cluster, "grpc", grpc_service, grpc_method, success);
}

Optional<Status::GrpcStatus> Common::getGrpcStatus(const Http::HeaderMap& headers) {
  const Http::HeaderEntry* grpc_status_header = headers.GrpcStatus();

  uint64_t grpc_status_code;
  if (!grpc_status_header || grpc_status_header->value().empty()) {
    return Optional<Status::GrpcStatus>();
  }
  if (!StringUtil::atoul(grpc_status_header->value().c_str(), grpc_status_code) ||
      grpc_status_code > Status::GrpcStatus::Unauthenticated) {
    return Optional<Status::GrpcStatus>(Status::GrpcStatus::InvalidCode);
  }
  return Optional<Status::GrpcStatus>(static_cast<Status::GrpcStatus>(grpc_status_code));
}

bool Common::resolveServiceAndMethod(const Http::HeaderEntry* path, std::string* service,
                                     std::string* method) {
  if (path == nullptr || path->value().c_str() == nullptr) {
    return false;
  }
  std::vector<std::string> parts = StringUtil::split(path->value().c_str(), '/');
  if (parts.size() != 2) {
    return false;
  }
  *service = std::move(parts[0]);
  *method = std::move(parts[1]);
  return true;
}

Status::GrpcStatus Common::httpToGrpcStatus(uint64_t http_response_status) {
  // From
  // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md.
  switch (http_response_status) {
  case 400:
    return Status::GrpcStatus::Internal;
  case 401:
    return Status::GrpcStatus::Unauthenticated;
  case 403:
    return Status::GrpcStatus::PermissionDenied;
  case 404:
    return Status::GrpcStatus::Unimplemented;
  case 429:
  case 502:
  case 503:
  case 504:
    return Status::GrpcStatus::Unavailable;
  default:
    return Status::GrpcStatus::Unknown;
  }
}

Buffer::InstancePtr Common::serializeBody(const google::protobuf::Message& message) {
  // http://www.grpc.io/docs/guides/wire.html
  // Reserve enough space for the entire message and the 5 byte header.
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
  google::protobuf::io::ArrayOutputStream stream(current, size, -1);
  google::protobuf::io::CodedOutputStream codec_stream(&stream);
  message.SerializeWithCachedSizes(&codec_stream);
  body->commit(&iovec, 1);
  return body;
}

Http::MessagePtr Common::prepareHeaders(const std::string& upstream_cluster,
                                        const std::string& service_full_name,
                                        const std::string& method_name) {
  Http::MessagePtr message(new Http::RequestMessageImpl());
  message->headers().insertMethod().value(Http::Headers::get().MethodValues.Post);
  message->headers().insertPath().value().append("/", 1);
  message->headers().insertPath().value().append(service_full_name.c_str(),
                                                 service_full_name.size());
  message->headers().insertPath().value().append("/", 1);
  message->headers().insertPath().value().append(method_name.c_str(), method_name.size());
  message->headers().insertHost().value(upstream_cluster);
  message->headers().insertContentType().value(Common::GRPC_CONTENT_TYPE);

  return message;
}

void Common::checkForHeaderOnlyError(Http::Message& http_response) {
  // First check for grpc-status in headers. If it is here, we have an error.
  Optional<Status::GrpcStatus> grpc_status_code = Common::getGrpcStatus(http_response.headers());
  if (!grpc_status_code.valid()) {
    return;
  }

  if (grpc_status_code.value() == Status::GrpcStatus::InvalidCode) {
    throw Exception(Optional<uint64_t>(), "bad grpc-status header");
  }

  const Http::HeaderEntry* grpc_status_message = http_response.headers().GrpcMessage();
  throw Exception(grpc_status_code.value(),
                  grpc_status_message ? grpc_status_message->value().c_str() : EMPTY_STRING);
}

void Common::validateResponse(Http::Message& http_response) {
  if (Http::Utility::getResponseStatus(http_response.headers()) != enumToInt(Http::Code::OK)) {
    throw Exception(Optional<uint64_t>(), "non-200 response code");
  }

  checkForHeaderOnlyError(http_response);

  // Check for existence of trailers.
  if (!http_response.trailers()) {
    throw Exception(Optional<uint64_t>(), "no response trailers");
  }

  Optional<Status::GrpcStatus> grpc_status_code = Common::getGrpcStatus(*http_response.trailers());
  if (!grpc_status_code.valid() || grpc_status_code.value() < 0) {
    throw Exception(Optional<uint64_t>(), "bad grpc-status trailer");
  }

  if (grpc_status_code.value() != 0) {
    const Http::HeaderEntry* grpc_status_message = http_response.trailers()->GrpcMessage();
    throw Exception(grpc_status_code.value(),
                    grpc_status_message ? grpc_status_message->value().c_str() : EMPTY_STRING);
  }
}

} // Grpc
} // Envoy
