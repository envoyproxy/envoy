#include "source/extensions/filters/http/grpc_field_extraction/message_converter/stream_message.h"

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "proto_field_extraction/message_data/cord_message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {
namespace {

TEST(StreamMessage, SetterGetter) {
  auto msg = std::make_unique<Protobuf::field_extraction::CordMessageData>(absl::Cord("aa"));
  auto* msg_addr = msg.get();
  StreamMessage stream_message(nullptr, nullptr, nullptr);

  EXPECT_EQ(stream_message.message(), nullptr);
  stream_message.set(std::move(msg));
  EXPECT_EQ(stream_message.message(), msg_addr);
}

} // namespace
} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
