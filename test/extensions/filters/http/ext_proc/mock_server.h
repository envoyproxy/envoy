#pragma once

#include "source/extensions/filters/http/ext_proc/client_impl.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

namespace CommonExtProc = Envoy::Extensions::Common::ExternalProcessing;

class MockClient : public ExternalProcessorClient {
public:
  MockClient();
  ~MockClient() override;

  MOCK_METHOD(ExternalProcessorStreamPtr, start,
              (ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
               Envoy::Http::AsyncClient::StreamOptions&,
               Envoy::Http::StreamFilterSidestreamWatermarkCallbacks&),
              (override));
  MOCK_METHOD(void, sendRequest,
              (envoy::service::ext_proc::v3::ProcessingRequest&&, bool, const uint64_t,
               CommonExtProc::RequestCallbacks<envoy::service::ext_proc::v3::ProcessingResponse>*,
               CommonExtProc::StreamBase*),
              (override));
  MOCK_METHOD(void, cancel, ());

  MOCK_METHOD(const Envoy::StreamInfo::StreamInfo*, getStreamInfo, (), (const));
};

class MockStream : public ExternalProcessorStream {
public:
  MockStream();
  ~MockStream() override;
  MOCK_METHOD(void, send, (envoy::service::ext_proc::v3::ProcessingRequest&&, bool));
  MOCK_METHOD(bool, close, ());
  MOCK_METHOD(bool, halfCloseAndDeleteOnRemoteClose, ());
  MOCK_METHOD(const StreamInfo::StreamInfo&, streamInfo, (), (const override));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(void, notifyFilterDestroy, ());
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
