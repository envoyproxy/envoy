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
  MockStream();
  ~MockStream() override;

  // ExtAuthz::Client
  MOCK_METHOD(void, send,
              (envoy::service::ext_proc::v3::ProcessingRequest&& request,
               bool end_stream));
  MOCK_METHOD(bool, close, ());
};


class MockClient : public ExternalProcessing::ExternalProcessorClient {
 public:
  MockClient();
  ~MockClient() override;

  // ExtAuthz::Client
  MOCK_METHOD(ExternalProcessing::ExternalProcessorStreamPtr, start,
              (ExternalProcessing::ExternalProcessorCallbacks& callbacks,
               const envoy::config::core::v3::GrpcService& grpc_service,
               const StreamInfo::StreamInfo& stream_info));
};

class MockRequestCallbacks : public ExternalProcessing::ExternalProcessorCallbacks {
 public:
  MockRequestCallbacks();
  ~MockRequestCallbacks() override;

  MOCK_METHOD(void, onReceiveMessage,
              (std::unique_ptr<envoy::service::ext_proc::v3::ProcessingResponse>&& response));
  MOCK_METHOD(void, onGrpcError, (Grpc::Status::GrpcStatus error));
  MOCK_METHOD(void, onGrpcClose, ());
};

}  // namespace UnitTestFuzz
}  // namespace ExtProc
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy
