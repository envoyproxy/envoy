#pragma once

#include "source/extensions/filters/http/ext_proc/client.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class MockClient : public ExternalProcessorClient {
public:
  MockClient();
  ~MockClient() override;
  MOCK_METHOD(ExternalProcessorStreamPtr, start,
              (ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
               const StreamInfo::StreamInfo&));
};

class MockStream : public ExternalProcessorStream {
public:
  MockStream();
  ~MockStream() override;
  MOCK_METHOD(void, send, (envoy::service::ext_proc::v3::ProcessingRequest&&, bool));
  MOCK_METHOD(bool, close, ());
  MOCK_METHOD(const StreamInfo::StreamInfo&, streamInfo, (), (const override));
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
