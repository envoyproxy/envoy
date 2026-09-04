#include <map>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/macros.h"
#include "source/common/json/proto_streamer.h"
#include "source/common/protobuf/utility.h"

#include "test/benchmark/main.h"
#include "test/proto/extraction.pb.h"
#include "test/proto/sensitive.pb.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Json {
namespace {

enum class MessageType {
  Strings,
  Int64s,
  Uint32s,
  Doubles,
  Enums,
  Bytes,
  Messages,
  Maps,
  Anys,
};

extraction::TestBucket makeBucket(uint32_t index) {
  extraction::TestBucket bucket;
  bucket.set_name(absl::StrCat("bucket_", index));
  bucket.set_ratio(0.5);
  bucket.add_objects("payload");
  return bucket;
}

ProtobufTypes::MessagePtr makeMessage(MessageType type, uint32_t elements) {
  switch (type) {
  case MessageType::Strings:
  case MessageType::Int64s:
  case MessageType::Uint32s:
  case MessageType::Doubles:
  case MessageType::Enums: {
    auto request = std::make_unique<extraction::TestRequest>();
    for (uint32_t i = 0; i < elements; i++) {
      switch (type) {
      case MessageType::Strings:
        request->add_repeated_strings(absl::StrCat("element_", i));
        break;
      case MessageType::Int64s:
        request->add_repeated_int64(i);
        break;
      case MessageType::Uint32s:
        request->add_repeated_uint32(i);
        break;
      case MessageType::Doubles:
        request->add_repeated_double(i + 0.5);
        break;
      case MessageType::Enums:
        request->add_repeated_enum(extraction::BETA);
        break;
      default:
        break;
      }
    }
    return request;
  }
  case MessageType::Bytes: {
    auto bucket = std::make_unique<extraction::TestBucket>();
    bucket->set_name("bytes");
    for (uint32_t i = 0; i < elements; i++) {
      bucket->add_objects(absl::StrCat("object_", i));
    }
    return bucket;
  }
  case MessageType::Messages: {
    auto response = std::make_unique<extraction::TestResponse>();
    for (uint32_t i = 0; i < elements; i++) {
      *response->add_buckets() = makeBucket(i);
    }
    return response;
  }
  case MessageType::Maps: {
    auto response = std::make_unique<extraction::TestResponse>();
    for (uint32_t i = 0; i < elements; i++) {
      (*response->mutable_sub_buckets())[absl::StrCat("key_", i)] = makeBucket(i);
    }
    return response;
  }
  case MessageType::Anys: {
    auto any_holder = std::make_unique<envoy::test::Sensitive>();
    for (uint32_t i = 0; i < elements; i++) {
      std::ignore = any_holder->add_insensitive_repeated_any()->PackFrom(makeBucket(i));
    }
    return any_holder;
  }
  }
  return nullptr;
}

using MessageCache = std::map<std::pair<MessageType, uint32_t>, ProtobufTypes::MessagePtr>;
MessageCache& messageCache() { MUTABLE_CONSTRUCT_ON_FIRST_USE(MessageCache); }

const Protobuf::Message& cachedMessage(MessageType type, uint32_t elements) {
  MessageCache& cache = messageCache();
  const std::pair<MessageType, uint32_t> key{type, elements};
  const auto cached = cache.find(key);
  if (cached != cache.end()) {
    return *cached->second;
  }
  return *cache.emplace(key, makeMessage(type, elements)).first->second;
}

uint64_t streamMessage(const Protobuf::Message& message) {
  Buffer::OwnedImpl buffer;
  uint64_t bytes = 0;
  {
    BufferStreamer streamer(buffer);
    BufferStreamer::ArrayPtr array = streamer.makeRootArray();
    MessageStreamer message_streamer(message, *array,
                                     {.emit_type_url_ = true,
                                      .preserve_proto_field_names_ = true,
                                      .redact_sensitive_fields_ = true});
    while (message_streamer.next()) {
      bytes += buffer.length();
      buffer.drain(buffer.length());
    }
  }
  return bytes + buffer.length();
}

uint64_t printMessage(const Protobuf::Message& message) {
  return MessageUtil::getJsonStringFromMessageOrError(message).size();
}

uint32_t elementCount(const ::benchmark::State& state) {
  const auto elements = static_cast<uint32_t>(state.range(0));
  return benchmark::skipExpensiveBenchmarks() ? std::min(elements, 1000U) : elements;
}

void elementCounts(::benchmark::internal::Benchmark* registration) {
  registration->Arg(1000)->Arg(10000)->Unit(::benchmark::kMicrosecond);
}

void report(::benchmark::State& state, uint64_t bytes) {
  state.SetBytesProcessed(static_cast<int64_t>(bytes) * state.iterations());
  state.SetItemsProcessed(static_cast<int64_t>(elementCount(state)) * state.iterations());
  state.counters["response_bytes"] = bytes;
}

// NOLINTNEXTLINE(readability-identifier-naming)
void BM_Print(::benchmark::State& state, MessageType type) {
  const Protobuf::Message& message = cachedMessage(type, elementCount(state));
  uint64_t bytes = 0;
  for (auto _ : state) { // NOLINT
    bytes = printMessage(message);
  }
  report(state, bytes);
}

// NOLINTNEXTLINE(readability-identifier-naming)
void BM_Stream(::benchmark::State& state, MessageType type) {
  const Protobuf::Message& message = cachedMessage(type, elementCount(state));
  uint64_t bytes = 0;
  for (auto _ : state) { // NOLINT
    bytes = streamMessage(message);
  }
  report(state, bytes);
}

#define MESSAGE_BENCHMARKS(row_name, type)                                                         \
  BENCHMARK_CAPTURE(BM_Print, row_name, type)->Apply(elementCounts);                               \
  BENCHMARK_CAPTURE(BM_Stream, row_name, type)->Apply(elementCounts)

MESSAGE_BENCHMARKS(strings, MessageType::Strings);
MESSAGE_BENCHMARKS(int64s, MessageType::Int64s);
MESSAGE_BENCHMARKS(uint32s, MessageType::Uint32s);
MESSAGE_BENCHMARKS(doubles, MessageType::Doubles);
MESSAGE_BENCHMARKS(enums, MessageType::Enums);
MESSAGE_BENCHMARKS(bytes, MessageType::Bytes);
MESSAGE_BENCHMARKS(messages, MessageType::Messages);
MESSAGE_BENCHMARKS(maps, MessageType::Maps);
MESSAGE_BENCHMARKS(anys, MessageType::Anys);

#undef MESSAGE_BENCHMARKS

} // namespace
} // namespace Json
} // namespace Envoy
