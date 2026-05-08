#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using envoy::config::core::v3::TrafficDirection;
using Filters::Common::ProcessingEffect::Effect;

class ExtProcLoggingInfoTest : public ::testing::Test {
public:
  ExtProcLoggingInfoTest() : logging_info_(filter_metadata_) {}

  Protobuf::Struct filter_metadata_;
  ExtProcLoggingInfo logging_info_;
};

TEST_F(ExtProcLoggingInfoTest, SerializeEmpty) {
  auto proto = logging_info_.serializeAsProto();
  EXPECT_TRUE(proto != nullptr);

  auto str = logging_info_.serializeAsString();
  EXPECT_TRUE(str.has_value());
  EXPECT_EQ(str.value(), "bs:0,br:0,os:0");
}

TEST_F(ExtProcLoggingInfoTest, RecordAndSerialize) {
  logging_info_.recordGrpcCall(std::chrono::microseconds(100), Grpc::Status::Ok,
                               ProcessorState::CallbackState::HeadersCallback,
                               TrafficDirection::INBOUND);
  logging_info_.recordGrpcCall(std::chrono::microseconds(200), Grpc::Status::Aborted,
                               ProcessorState::CallbackState::TrailersCallback,
                               TrafficDirection::INBOUND);
  logging_info_.recordGrpcCall(std::chrono::microseconds(300), Grpc::Status::Ok,
                               ProcessorState::CallbackState::BufferedBodyCallback,
                               TrafficDirection::INBOUND);
  logging_info_.recordGrpcCall(std::chrono::microseconds(400), Grpc::Status::Ok,
                               ProcessorState::CallbackState::BufferedBodyCallback,
                               TrafficDirection::INBOUND);

  logging_info_.recordGrpcCall(std::chrono::microseconds(500), Grpc::Status::Ok,
                               ProcessorState::CallbackState::HeadersCallback,
                               TrafficDirection::OUTBOUND);
  logging_info_.recordGrpcCall(std::chrono::microseconds(600), Grpc::Status::Ok,
                               ProcessorState::CallbackState::TrailersCallback,
                               TrafficDirection::OUTBOUND);
  logging_info_.recordGrpcCall(std::chrono::microseconds(700), Grpc::Status::Ok,
                               ProcessorState::CallbackState::BufferedBodyCallback,
                               TrafficDirection::OUTBOUND);

  logging_info_.setBytesSent(1000);
  logging_info_.setBytesReceived(2000);
  logging_info_.recordGrpcStatusBeforeFirstCall(Grpc::Status::Internal);
  logging_info_.setFailedOpen();
  logging_info_.setReceivedImmediateResponse();

  logging_info_.recordProcessingEffect(ProcessorState::CallbackState::HeadersCallback,
                                       TrafficDirection::INBOUND, Effect::MutationApplied);
  logging_info_.recordProcessingEffect(ProcessorState::CallbackState::TrailersCallback,
                                       TrafficDirection::INBOUND, Effect::MutationFailed);
  logging_info_.recordProcessingEffect(ProcessorState::CallbackState::BufferedBodyCallback,
                                       TrafficDirection::INBOUND, Effect::None);

  auto proto = logging_info_.serializeAsProto();
  ASSERT_TRUE(proto != nullptr);
  const auto& fields = dynamic_cast<const Protobuf::Struct&>(*proto).fields();

  EXPECT_EQ(fields.at("request_header_latency_us").number_value(), 100);
  EXPECT_EQ(fields.at("request_trailer_latency_us").number_value(), 200);
  EXPECT_EQ(fields.at("request_body_call_count").number_value(), 2);
  EXPECT_EQ(fields.at("request_body_total_latency_us").number_value(), 700);
  EXPECT_EQ(fields.at("request_body_max_latency_us").number_value(), 400);

  EXPECT_EQ(fields.at("bytes_sent").number_value(), 1000);
  EXPECT_EQ(fields.at("bytes_received").number_value(), 2000);
  EXPECT_TRUE(fields.at("failed_open").bool_value());
  EXPECT_TRUE(fields.at("received_immediate_response").bool_value());
  EXPECT_EQ(fields.at("grpc_status_before_first_call").number_value(),
            static_cast<int>(Grpc::Status::Internal));

  auto str = logging_info_.serializeAsString();
  EXPECT_TRUE(str.has_value());
  // `rh:100:0,rb:2:700:0,rt:200:10,sh:500:0,sb:1:700:0,st:600:0,bs:1000,br:2000,os:13`
  EXPECT_THAT(str.value(), testing::HasSubstr("rh:100:0"));
  EXPECT_THAT(str.value(), testing::HasSubstr("rb:2:700:0"));
  EXPECT_THAT(str.value(), testing::HasSubstr("rt:200:10"));
  EXPECT_THAT(str.value(), testing::HasSubstr("bs:1000"));
  EXPECT_THAT(str.value(), testing::HasSubstr("br:2000"));
  EXPECT_THAT(str.value(), testing::HasSubstr("os:13"));
}

TEST_F(ExtProcLoggingInfoTest, GetField) {
  logging_info_.recordGrpcCall(std::chrono::microseconds(100), Grpc::Status::Ok,
                               ProcessorState::CallbackState::HeadersCallback,
                               TrafficDirection::INBOUND);
  logging_info_.setBytesSent(1000);
  logging_info_.setFailedOpen();

  EXPECT_EQ(absl::get<int64_t>(logging_info_.getField("request_header_latency_us")), 100);
  EXPECT_EQ(absl::get<int64_t>(logging_info_.getField("bytes_sent")), 1000);
  EXPECT_EQ(absl::get<int64_t>(logging_info_.getField("failed_open")), 1);
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(logging_info_.getField("non_existent")));
}

TEST_F(ExtProcLoggingInfoTest, ProcessingEffectsConst) {
  const auto& const_logging_info = logging_info_;

  // Exercise both directions
  EXPECT_EQ(Effect::None,
            const_logging_info.processingEffects(TrafficDirection::INBOUND).header_effect_);
  EXPECT_EQ(Effect::None,
            const_logging_info.processingEffects(TrafficDirection::OUTBOUND).header_effect_);
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
