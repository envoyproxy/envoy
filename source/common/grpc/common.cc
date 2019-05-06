#include "common/grpc/common.h"

#include <arpa/inet.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include "common/buffer/buffer_impl.h"
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

bool Common::hasGrpcContentType(const Http::HeaderMap& headers) {
  const Http::HeaderEntry* content_type = headers.ContentType();
  // Content type is gRPC if it is exactly "application/grpc" or starts with
  // "application/grpc+". Specifically, something like application/grpc-web is not gRPC.
  return content_type != nullptr &&
         absl::StartsWith(content_type->value().getStringView(),
                          Http::Headers::get().ContentTypeValues.Grpc) &&
         (content_type->value().size() == Http::Headers::get().ContentTypeValues.Grpc.size() ||
          content_type->value().c_str()[Http::Headers::get().ContentTypeValues.Grpc.size()] == '+');
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
      StringUtil::atoull(grpc_status->value().c_str(), grpc_status_code) && grpc_status_code == 0;
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
  if (!StringUtil::atoull(grpc_status_header->value().c_str(), grpc_status_code) ||
      grpc_status_code > Status::GrpcStatus::MaximumValid) {
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

Buffer::InstancePtr Common::serializeToGrpcFrame(const Protobuf::Message& message) {
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
    const char* unit =
        StringUtil::strtoull(header_grpc_timeout_entry->value().c_str(), grpc_timeout);
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

struct BufferInstanceContainer {
  BufferInstanceContainer(int ref_count, Buffer::InstancePtr buffer)
      : ref_count_(ref_count), buffer_(std::move(buffer)) {}
  std::atomic<int> ref_count_; // In case gPRC dereferences in a different threads.
  Buffer::InstancePtr buffer_;

  static void derefBufferInstanceContainer(void* container_ptr) {
    auto container = reinterpret_cast<BufferInstanceContainer*>(container_ptr);
    container->ref_count_--;
    // This is safe because the ref_count_ is never incremented.
    if (container->ref_count_ <= 0) {
      delete container;
    }
  }
};

grpc::ByteBuffer Common::makeByteBuffer(Buffer::InstancePtr&& buffer_instance) {
  if (!buffer_instance) {
    return {};
  }
  Buffer::RawSlice on_raw_slice;
  // NB: we need to pass in >= 1 in order to get the real "n" (see Buffer::Instance for details).
  int n_slices = buffer_instance->getRawSlices(&on_raw_slice, 1);
  if (n_slices <= 0) {
    return {};
  }
  auto container = new BufferInstanceContainer{n_slices, std::move(buffer_instance)};
  if (n_slices == 1) {
    grpc::Slice oneSlice(on_raw_slice.mem_, on_raw_slice.len_,
                         &BufferInstanceContainer::derefBufferInstanceContainer, container);
    return {&oneSlice, 1};
  }
  STACK_ARRAY(manyRawSlices, Buffer::RawSlice, n_slices);
  container->buffer_->getRawSlices(manyRawSlices.begin(), n_slices);
  std::vector<grpc::Slice> slices;
  slices.reserve(n_slices);
  for (int i = 0; i < n_slices; i++) {
    slices.emplace_back(manyRawSlices[i].mem_, manyRawSlices[i].len_,
                        &BufferInstanceContainer::derefBufferInstanceContainer, container);
  }
  return {&slices[0], slices.size()};
}

struct ByteBufferContainer {
  ByteBufferContainer(int ref_count) : ref_count_(ref_count) {}
  ~ByteBufferContainer() { ::free(fragments_); }
  std::atomic<int> ref_count_; // In case gPRC dereferences in a different threads.
  Buffer::BufferFragmentImpl* fragments_ = 0;
  std::vector<grpc::Slice> slices_;
};

Buffer::InstancePtr Common::makeBufferInstance(const grpc::ByteBuffer& byte_buffer) {
  auto buffer = std::make_unique<Buffer::OwnedImpl>();
  if (byte_buffer.Length() == 0) {
    return buffer;
  }
  // NB: ByteBuffer::Dump moves the data out of the ByteBuffer so we need to ensure that the
  // lifetime of the Slice(s) exceeds our Buffer::Instance.
  std::vector<grpc::Slice> slices;
  byte_buffer.Dump(&slices);
  if (slices.size() == 0) {
    return buffer;
  }
  auto* container = new ByteBufferContainer(static_cast<int>(slices.size()));
  std::function<void(const void*, size_t, const Buffer::BufferFragmentImpl*)> releaser =
      [container](const void*, size_t, const Buffer::BufferFragmentImpl*) {
        container->ref_count_--;
        if (container->ref_count_ <= 0) {
          delete container;
        }
      };
  // NB: addBufferFragment takes a pointer alias to the BufferFragmentImpl which is passed in so we
  // need to ensure that the lifetime of those objects exceeds that of the Buffer::Instance.
  ASSERT(!::posix_memalign(reinterpret_cast<void**>(&container->fragments_),
                           alignof(Buffer::BufferFragmentImpl),
                           sizeof(Buffer::BufferFragmentImpl) * slices.size()));
  for (size_t i = 0; i < slices.size(); i++) {
    new (&container->fragments_[i])
        Buffer::BufferFragmentImpl(slices[i].begin(), slices[i].size(), releaser);
  }
  for (size_t i = 0; i < slices.size(); i++) {
    buffer->addBufferFragment(container->fragments_[i]);
  }
  container->slices_ = std::move(slices);
  return buffer;
}

void Common::PrependGrpcFrameHeader(Buffer::Instance& buffer) {
  char header[5];
  header[0] = 0; // flags
  const uint32_t nsize = htonl(buffer.length());
  std::memcpy(&header[1], reinterpret_cast<const void*>(&nsize), sizeof(uint32_t));
  buffer.prepend(absl::string_view(header, 5));
}

} // namespace Grpc
} // namespace Envoy
