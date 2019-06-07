#include "common/grpc/common.h"

#include <arpa/inet.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/macros.h"
#include "common/common/stack_array.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/protobuf/protobuf.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Grpc {

Common::Common(Stats::SymbolTable& symbol_table)
    : symbol_table_(symbol_table), stat_name_pool_(symbol_table),
      grpc_(stat_name_pool_.add("grpc")), grpc_web_(stat_name_pool_.add("grpc-web")),
      success_(stat_name_pool_.add("success")), failure_(stat_name_pool_.add("failure")),
      total_(stat_name_pool_.add("total")), zero_(stat_name_pool_.add("0")) {}

// Makes a stat name from a string, if we don't already have one for it.
// This always takes a lock on mutex_, and if we haven't seen the name
// before, it also takes a lock on the symbol table.
//
// TODO(jmarantz): See https://github.com/envoyproxy/envoy/pull/7008 for
// a lock-free approach to creating dynamic stat-names based on requests.
Stats::StatName Common::makeDynamicStatName(absl::string_view name) {
  Thread::LockGuard lock(mutex_);
  auto iter = stat_name_map_.find(name);
  if (iter != stat_name_map_.end()) {
    return iter->second;
  }
  const Stats::StatName stat_name = stat_name_pool_.add(name);
  stat_name_map_[std::string(name)] = stat_name;
  return stat_name;
}

bool Common::hasGrpcContentType(const Http::HeaderMap& headers) {
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

void Common::chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                        const RequestNames& request_names, const Http::HeaderEntry* grpc_status) {
  if (!grpc_status) {
    return;
  }

  absl::string_view status_str = grpc_status->value().getStringView();
  const bool success = (status_str == "0");

  // TODO(jmarantz): Perhaps the universe of likely grpc status codes is
  // sufficiently bounded that we should precompute status StatNames for popular
  // ones beyond "0".
  const Stats::StatName status_stat_name = success ? zero_ : makeDynamicStatName(status_str);
  const Stats::SymbolTable::StoragePtr stat_name_storage =
      symbol_table_.join({protocolStatName(protocol), request_names.service_, request_names.method_,
                          status_stat_name});

  cluster.statsScope().counterFromStatName(Stats::StatName(stat_name_storage.get())).inc();
  chargeStat(cluster, protocol, request_names, success);
}

void Common::chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                        const RequestNames& request_names, bool success) {
  const Stats::SymbolTable::StoragePtr prefix_storage = symbol_table_.join(
      {protocolStatName(protocol), request_names.service_, request_names.method_});
  const Stats::StatName prefix(prefix_storage.get());
  const Stats::SymbolTable::StoragePtr status =
      symbol_table_.join({prefix, successStatName(success)});
  const Stats::SymbolTable::StoragePtr total = symbol_table_.join({prefix, total_});

  cluster.statsScope().counterFromStatName(Stats::StatName(status.get())).inc();
  cluster.statsScope().counterFromStatName(Stats::StatName(total.get())).inc();
}

void Common::chargeStat(const Upstream::ClusterInfo& cluster, const RequestNames& request_names,
                        bool success) {
  chargeStat(cluster, Protocol::Grpc, request_names, success);
}

absl::optional<Status::GrpcStatus> Common::getGrpcStatus(const Http::HeaderMap& trailers) {
  const Http::HeaderEntry* grpc_status_header = trailers.GrpcStatus();

  uint64_t grpc_status_code;
  if (!grpc_status_header || grpc_status_header->value().empty()) {
    return absl::optional<Status::GrpcStatus>();
  }
  if (!absl::SimpleAtoi(grpc_status_header->value().getStringView(), &grpc_status_code) ||
      grpc_status_code > Status::GrpcStatus::MaximumValid) {
    return absl::optional<Status::GrpcStatus>(Status::GrpcStatus::InvalidCode);
  }
  return absl::optional<Status::GrpcStatus>(static_cast<Status::GrpcStatus>(grpc_status_code));
}

std::string Common::getGrpcMessage(const Http::HeaderMap& trailers) {
  const auto entry = trailers.GrpcMessage();
  return entry ? std::string(entry->value().getStringView()) : EMPTY_STRING;
}

absl::optional<Common::RequestNames>
Common::resolveServiceAndMethod(const Http::HeaderEntry* path) {
  absl::optional<RequestNames> request_names;
  if (path == nullptr) {
    return request_names;
  }
  const auto parts = StringUtil::splitToken(path->value().getStringView(), "/");
  if (parts.size() != 2) {
    return request_names;
  }
  const Stats::StatName service = makeDynamicStatName(parts[0]);
  const Stats::StatName method = makeDynamicStatName(parts[1]);
  request_names = RequestNames{service, method};
  return request_names;
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

std::chrono::milliseconds Common::getGrpcTimeout(Http::HeaderMap& request_headers) {
  std::chrono::milliseconds timeout(0);
  Http::HeaderEntry* header_grpc_timeout_entry = request_headers.GrpcTimeout();
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

void Common::toGrpcTimeout(const std::chrono::milliseconds& timeout, Http::HeaderString& value) {
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
  value.setInteger(time);
  value.append(unit, 1);
}

Http::MessagePtr Common::prepareHeaders(const std::string& upstream_cluster,
                                        const std::string& service_full_name,
                                        const std::string& method_name,
                                        const absl::optional<std::chrono::milliseconds>& timeout) {
  Http::MessagePtr message(new Http::RequestMessageImpl());
  message->headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Post);
  message->headers().insertPath().value().append("/", 1);
  message->headers().insertPath().value().append(service_full_name.c_str(),
                                                 service_full_name.size());
  message->headers().insertPath().value().append("/", 1);
  message->headers().insertPath().value().append(method_name.c_str(), method_name.size());
  message->headers().insertHost().value(upstream_cluster);
  // According to https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md TE should appear
  // before Timeout and ContentType.
  message->headers().insertTE().value().setReference(Http::Headers::get().TEValues.Trailers);
  if (timeout) {
    toGrpcTimeout(timeout.value(), message->headers().insertGrpcTimeout().value());
  }
  message->headers().insertContentType().value().setReference(
      Http::Headers::get().ContentTypeValues.Grpc);

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

  throw Exception(grpc_status_code.value(), Common::getGrpcMessage(http_response.headers()));
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
