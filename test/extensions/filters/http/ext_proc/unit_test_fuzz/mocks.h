#pragma once

#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/extensions/filters/http/ext_proc/client.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtProc {
namespace UnitTestFuzz {

class MockStream : public ExternalProcessing::ExternalProcessorStream {
public:
  MockStream() = default;
  ~MockStream() override = default;

  MOCK_METHOD(void, send,
              (envoy::service::ext_proc::v3::ProcessingRequest && request, bool end_stream));
  MOCK_METHOD(bool, close, ());
  MOCK_METHOD(const StreamInfo::StreamInfo&, streamInfo, (), (const override));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(void, notifyFilterDestroy, ());
};

class MockClient : public ExternalProcessing::ExternalProcessorClient {
public:
  MockClient() {
    EXPECT_CALL(*this, stream()).WillRepeatedly(testing::Invoke([this]() { return stream_; }));
    EXPECT_CALL(*this, setStream(testing::_))
        .WillRepeatedly(
            testing::Invoke([this](ExternalProcessing::ExternalProcessorStream* stream) -> void {
              stream_ = stream;
            }));
  }
  ~MockClient() override = default;

  MOCK_METHOD(ExternalProcessing::ExternalProcessorStreamPtr, start,
              (ExternalProcessing::ExternalProcessorCallbacks & callbacks,
               const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
               const Envoy::Http::AsyncClient::StreamOptions&,
               Envoy::Http::StreamFilterSidestreamWatermarkCallbacks&));
  MOCK_METHOD(ExternalProcessing::ExternalProcessorStream*, stream, ());
  MOCK_METHOD(void, setStream, (ExternalProcessing::ExternalProcessorStream * stream));
  ExternalProcessing::ExternalProcessorStream* stream_ = nullptr;
};

} // namespace UnitTestFuzz
} // namespace ExtProc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
