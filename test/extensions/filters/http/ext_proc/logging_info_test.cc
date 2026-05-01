#include "source/extensions/filters/http/ext_proc/ext_proc.h"
#include "test/test_common/utility.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

TEST(ExtProcLoggingInfoTest, SerializeAsProto) {
  Protobuf::Struct metadata;
  (*metadata.mutable_fields())["foo"] = ValueUtil::stringValue("bar");
  ExtProcLoggingInfo info(metadata);

  info.recordGrpcCall(std::chrono::microseconds(100), Grpc::Status::Ok,
                      ProcessorState::CallbackState::HeadersCallback,
                      envoy::config::core::v3::TrafficDirection::INBOUND);
  info.recordGrpcCall(std::chrono::microseconds(200), Grpc::Status::Aborted,
                      ProcessorState::CallbackState::BufferedBodyCallback,
                      envoy::config::core::v3::TrafficDirection::INBOUND);
  info.recordGrpcCall(std::chrono::microseconds(300), Grpc::Status::Internal,
                      ProcessorState::CallbackState::TrailersCallback,
                      envoy::config::core::v3::TrafficDirection::INBOUND);

  info.recordGrpcCall(std::chrono::microseconds(400), Grpc::Status::Ok,
                      ProcessorState::CallbackState::HeadersCallback,
                      envoy::config::core::v3::TrafficDirection::OUTBOUND);
  info.recordGrpcCall(std::chrono::microseconds(500), Grpc::Status::Aborted,
                      ProcessorState::CallbackState::BufferedBodyCallback,
                      envoy::config::core::v3::TrafficDirection::OUTBOUND);
  info.recordGrpcCall(std::chrono::microseconds(600), Grpc::Status::Internal,
                      ProcessorState::CallbackState::TrailersCallback,
                      envoy::config::core::v3::TrafficDirection::OUTBOUND);

  info.setBytesSent(1000);
  info.setBytesReceived(2000);
  info.setFailedOpen();
  info.setReceivedImmediateResponse();
  info.recordGrpcStatusBeforeFirstCall(Grpc::Status::DeadlineExceeded);

  info.recordProcessingEffect(ProcessorState::CallbackState::HeadersCallback,
                              envoy::config::core::v3::TrafficDirection::INBOUND,
                              Filters::Common::ProcessingEffect::Effect::MutationRejectedSizeLimitExceeded);
  info.recordProcessingEffect(ProcessorState::CallbackState::BufferedBodyCallback,
                              envoy::config::core::v3::TrafficDirection::INBOUND,
                              Filters::Common::ProcessingEffect::Effect::InvalidMutationRejected);
  info.recordProcessingEffect(ProcessorState::CallbackState::TrailersCallback,
                              envoy::config::core::v3::TrafficDirection::INBOUND,
                              Filters::Common::ProcessingEffect::Effect::None);

  auto proto = info.serializeAsProto();
  auto* struct_ptr = dynamic_cast<Protobuf::Struct*>(proto.get());
  ASSERT_NE(struct_ptr, nullptr);

  const auto& fields = struct_ptr->fields();
  EXPECT_EQ(100.0, fields.at("request_header_latency_us").number_value());
  EXPECT_EQ(0.0, fields.at("request_header_call_status").number_value());
  EXPECT_EQ(1.0, fields.at("request_body_call_count").number_value());
  EXPECT_EQ(200.0, fields.at("request_body_total_latency_us").number_value());
  EXPECT_EQ(200.0, fields.at("request_body_max_latency_us").number_value());
  EXPECT_EQ(10.0, fields.at("request_body_last_call_status").number_value());
  EXPECT_EQ(300.0, fields.at("request_trailer_latency_us").number_value());
  EXPECT_EQ(13.0, fields.at("request_trailer_call_status").number_value());

  EXPECT_EQ(400.0, fields.at("response_header_latency_us").number_value());
  EXPECT_EQ(0.0, fields.at("response_header_call_status").number_value());
  EXPECT_EQ(1.0, fields.at("response_body_call_count").number_value());
  EXPECT_EQ(500.0, fields.at("response_body_total_latency_us").number_value());
  EXPECT_EQ(500.0, fields.at("response_body_max_latency_us").number_value());
  EXPECT_EQ(10.0, fields.at("response_body_last_call_status").number_value());
  EXPECT_EQ(600.0, fields.at("response_trailer_latency_us").number_value());
  EXPECT_EQ(13.0, fields.at("response_trailer_call_status").number_value());

  EXPECT_EQ(1000.0, fields.at("bytes_sent").number_value());
  EXPECT_EQ(2000.0, fields.at("bytes_received").number_value());
  EXPECT_TRUE(fields.at("failed_open").bool_value());
  EXPECT_TRUE(fields.at("received_immediate_response").bool_value());
  EXPECT_EQ(4.0, fields.at("grpc_status_before_first_call").number_value());
}

TEST(ExtProcLoggingInfoTest, SerializeAsString) {
  Protobuf::Struct metadata;
  ExtProcLoggingInfo info(metadata);
  
  info.recordGrpcCall(std::chrono::microseconds(100), Grpc::Status::Ok,
                      ProcessorState::CallbackState::HeadersCallback,
                      envoy::config::core::v3::TrafficDirection::INBOUND);
  info.recordGrpcCall(std::chrono::microseconds(200), Grpc::Status::Aborted,
                      ProcessorState::CallbackState::BufferedBodyCallback,
                      envoy::config::core::v3::TrafficDirection::INBOUND);
  info.recordGrpcCall(std::chrono::microseconds(300), Grpc::Status::Internal,
                      ProcessorState::CallbackState::TrailersCallback,
                      envoy::config::core::v3::TrafficDirection::INBOUND);

  info.recordGrpcCall(std::chrono::microseconds(400), Grpc::Status::Ok,
                      ProcessorState::CallbackState::HeadersCallback,
                      envoy::config::core::v3::TrafficDirection::OUTBOUND);
  info.recordGrpcCall(std::chrono::microseconds(500), Grpc::Status::Aborted,
                      ProcessorState::CallbackState::BufferedBodyCallback,
                      envoy::config::core::v3::TrafficDirection::OUTBOUND);
  info.recordGrpcCall(std::chrono::microseconds(600), Grpc::Status::Internal,
                      ProcessorState::CallbackState::TrailersCallback,
                      envoy::config::core::v3::TrafficDirection::OUTBOUND);

  info.setBytesSent(1000);
  info.setBytesReceived(2000);
  info.setFailedOpen();
  info.setReceivedImmediateResponse();
  info.recordGrpcStatusBeforeFirstCall(Grpc::Status::DeadlineExceeded);
  
  auto str = info.serializeAsString();
  EXPECT_TRUE(str.has_value());
  EXPECT_THAT(str.value(), testing::HasSubstr("rh:100:0"));
  EXPECT_THAT(str.value(), testing::HasSubstr("rb:1:200:10"));
  EXPECT_THAT(str.value(), testing::HasSubstr("sh:400:0"));
  EXPECT_THAT(str.value(), testing::HasSubstr("bs:1000"));
  EXPECT_THAT(str.value(), testing::HasSubstr("br:2000"));
}

TEST(ExtProcLoggingInfoTest, GetField) {
  Protobuf::Struct metadata;
  ExtProcLoggingInfo info(metadata);
  info.setBytesSent(123);
  info.setBytesReceived(456);
  info.setFailedOpen();
  
  EXPECT_THAT(info.getField("bytes_sent"), testing::VariantWith<int64_t>(123));
  EXPECT_THAT(info.getField("bytes_received"), testing::VariantWith<int64_t>(456));
  EXPECT_THAT(info.getField("failed_open"), testing::VariantWith<int64_t>(1));
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
