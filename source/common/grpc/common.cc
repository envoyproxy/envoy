#include "common/grpc/common.h"

#include <arpa/inet.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/macros.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Grpc {

bool Common::hasGrpcContentType(const Http::HeaderMap& headers) {
  const Http::HeaderEntry* content_type = headers.ContentType();
  if (content_type == nullptr) {
    return false;
  }
  // Fail fast if this is not gRPC.
  if (!StringUtil::startsWith(content_type->value().c_str(),
                              Http::Headers::get().ContentTypeValues.Grpc)) {
    return false;
  }
  // Exact match with application/grpc. This and the above case are likely the
  // two most common encountered.
  if (content_type->value() == Http::Headers::get().ContentTypeValues.Grpc.c_str()) {
    return true;
  }
  // Prefix match with application/grpc+. It's not sufficient to rely on the an
  // application/grpc prefix match, since there are related content types such as
  // application/grpc-web.
  if (content_type->value().size() > Http::Headers::get().ContentTypeValues.Grpc.size() &&
      content_type->value().c_str()[Http::Headers::get().ContentTypeValues.Grpc.size()] == '+') {
    return true;
  }
  // This must be something like application/grpc-web.
  return false;
}

bool Common::isGrpcResponseHeader(const Http::HeaderMap& headers, bool end_stream) {
  if (end_stream) {
    // Trailers-only response, only grpc-status is required.
    return headers.GrpcStatus() != nullptr;
  }
  if (Http::Utility::getResponseStatus(headers) != enumToInt(Http::Code::OK)) {
    return false;
  }
  return hasGrpcContentType(headers);
}

void Common::chargeStat(const Upstream::ClusterInfo& cluster, const std::string& protocol,
                        const std::string& grpc_service, const std::string& grpc_method,
                        const Http::HeaderEntry* grpc_status) {
  if (!grpc_status) {
    return;
  }
  cluster.statsScope()
      .counter(fmt::format("{}.{}.{}.{}", protocol, grpc_service, grpc_method,
                           grpc_status->value().c_str()))
      .inc();
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

absl::optional<Status::GrpcStatus> Common::getGrpcStatus(const Http::HeaderMap& trailers) {
  const Http::HeaderEntry* grpc_status_header = trailers.GrpcStatus();

  uint64_t grpc_status_code;
  if (!grpc_status_header || grpc_status_header->value().empty()) {
    return absl::optional<Status::GrpcStatus>();
  }
  if (!StringUtil::atoul(grpc_status_header->value().c_str(), grpc_status_code) ||
      grpc_status_code > Status::GrpcStatus::Unauthenticated) {
    return absl::optional<Status::GrpcStatus>(Status::GrpcStatus::InvalidCode);
  }
  return absl::optional<Status::GrpcStatus>(static_cast<Status::GrpcStatus>(grpc_status_code));
}

std::string Common::getGrpcMessage(const Http::HeaderMap& trailers) {
  const auto entry = trailers.GrpcMessage();
  return entry ? entry->value().c_str() : EMPTY_STRING;
}

bool Common::resolveServiceAndMethod(const Http::HeaderEntry* path, std::string* service,
                                     std::string* method) {
  if (path == nullptr || path->value().c_str() == nullptr) {
    return false;
  }
  const auto parts = StringUtil::splitToken(path->value().c_str(), "/");
  if (parts.size() != 2) {
    return false;
  }
  service->assign(parts[0].data(), parts[0].size());
  method->assign(parts[1].data(), parts[1].size());
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

uint64_t Common::grpcToHttpStatus(Status::GrpcStatus grpc_status) {
  // From https://cloud.google.com/apis/design/errors#handling_errors.
  switch (grpc_status) {
  case Status::GrpcStatus::Ok:
    return 200;
  case Status::GrpcStatus::Canceled:
    // Client closed request.
    return 499;
  case Status::GrpcStatus::Unknown:
    // Internal server error.
    return 500;
  case Status::GrpcStatus::InvalidArgument:
    // Bad request.
    return 400;
  case Status::GrpcStatus::DeadlineExceeded:
    // Gateway Time-out.
    return 504;
  case Status::GrpcStatus::NotFound:
    // Not found.
    return 404;
  case Status::GrpcStatus::AlreadyExists:
    // Conflict.
    return 409;
  case Status::GrpcStatus::PermissionDenied:
    // Forbidden.
    return 403;
  case Status::GrpcStatus::ResourceExhausted:
    //  Too many requests.
    return 429;
  case Status::GrpcStatus::FailedPrecondition:
    // Bad request.
    return 400;
  case Status::GrpcStatus::Aborted:
    // Conflict.
    return 409;
  case Status::GrpcStatus::OutOfRange:
    // Bad request.
    return 400;
  case Status::GrpcStatus::Unimplemented:
    // Not implemented.
    return 501;
  case Status::GrpcStatus::Internal:
    // Internal server error.
    return 500;
  case Status::GrpcStatus::Unavailable:
    // Service unavailable.
    return 503;
  case Status::GrpcStatus::DataLoss:
    // Internal server error.
    return 500;
  case Status::GrpcStatus::Unauthenticated:
    // Unauthorized.
    return 401;
  case Status::GrpcStatus::InvalidCode:
  default:
    // Internal server error.
    return 500;
  }
}

Buffer::InstancePtr Common::serializeBody(const Protobuf::Message& message) {
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
  Protobuf::io::ArrayOutputStream stream(current, size, -1);
  Protobuf::io::CodedOutputStream codec_stream(&stream);
  message.SerializeWithCachedSizes(&codec_stream);
  body->commit(&iovec, 1);
  return body;
}

Http::MessagePtr Common::prepareHeaders(const std::string& upstream_cluster,
                                        const std::string& service_full_name,
                                        const std::string& method_name) {
  Http::MessagePtr message(new Http::RequestMessageImpl());
  message->headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Post);
  message->headers().insertPath().value().append("/", 1);
  message->headers().insertPath().value().append(service_full_name.c_str(),
                                                 service_full_name.size());
  message->headers().insertPath().value().append("/", 1);
  message->headers().insertPath().value().append(method_name.c_str(), method_name.size());
  message->headers().insertHost().value(upstream_cluster);
  message->headers().insertContentType().value().setReference(
      Http::Headers::get().ContentTypeValues.Grpc);
  message->headers().insertTE().value().setReference(Http::Headers::get().TEValues.Trailers);

  return message;
}

void Common::checkForHeaderOnlyError(Http::Message& http_response) {
  // First check for grpc-status in headers. If it is here, we have an error.
  absl::optional<Status::GrpcStatus> grpc_status_code =
      Common::getGrpcStatus(http_response.headers());
  if (!grpc_status_code) {
    return;
  }

  if (grpc_status_code.value() == Status::GrpcStatus::InvalidCode) {
    throw Exception(absl::optional<uint64_t>(), "bad grpc-status header");
  }

  const Http::HeaderEntry* grpc_status_message = http_response.headers().GrpcMessage();
  throw Exception(grpc_status_code.value(),
                  grpc_status_message ? grpc_status_message->value().c_str() : EMPTY_STRING);
}

void Common::validateResponse(Http::Message& http_response) {
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
    const Http::HeaderEntry* grpc_status_message = http_response.trailers()->GrpcMessage();
    throw Exception(grpc_status_code.value(),
                    grpc_status_message ? grpc_status_message->value().c_str() : EMPTY_STRING);
  }
}

const std::string& Common::typeUrlPrefix() {
  CONSTRUCT_ON_FIRST_USE(std::string, "type.googleapis.com");
}

std::string Common::typeUrl(const std::string& qualified_name) {
  return typeUrlPrefix() + "/" + qualified_name;
}

} // namespace Grpc
} // namespace Envoy
