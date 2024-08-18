#pragma once

#include "source/extensions/filters/http/ext_proc/client.h"
#include "source/extensions/filters/http/ext_proc/http_client/client_base.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class MockClient : public ExternalProcessorClient,
                   public ClientBase {
public:
  MockClient();
  ~MockClient() override;
  void cancel() override {}
  void sendRequest() override {}

  MOCK_METHOD(ExternalProcessorStreamPtr, start,
              (ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
               const Envoy::Http::AsyncClient::StreamOptions&,
               Envoy::Http::StreamFilterSidestreamWatermarkCallbacks&));
};

class MockStream : public ExternalProcessorStream {
public:
  MockStream();
  ~MockStream() override;
  MOCK_METHOD(void, send, (envoy::service::ext_proc::v3::ProcessingRequest&&, bool));
  MOCK_METHOD(bool, close, ());
  MOCK_METHOD(const StreamInfo::StreamInfo&, streamInfo, (), (const override));
  MOCK_METHOD(void, notifyFilterDestroy, ());
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
