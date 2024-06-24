#pragma once

#include "source/extensions/config_subscription/grpc/grpc_stream_interface.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Config {

template <class RequestProto, class ResponseProto>
class MockGrpcStream : public GrpcStreamInterface<RequestProto, ResponseProto> {
public:
  MockGrpcStream() = default;
  ~MockGrpcStream() override = default;

  MOCK_METHOD(void, establishNewStream, ());
  MOCK_METHOD(bool, grpcStreamAvailable, (), (const));
  MOCK_METHOD(void, sendMessage, (const RequestProto& request));
  MOCK_METHOD(void, maybeUpdateQueueSizeStat, (uint64_t size));
  MOCK_METHOD(bool, checkRateLimitAllowsDrain, ());
  MOCK_METHOD(void, onCreateInitialMetadata, (Http::RequestHeaderMap & metadata));
  MOCK_METHOD(void, onReceiveInitialMetadata, (Http::ResponseHeaderMapPtr && metadata));
  MOCK_METHOD(void, onReceiveMessage, (ResponseProtoPtr<ResponseProto> && message));
  MOCK_METHOD(void, onReceiveTrailingMetadata, (Http::ResponseTrailerMapPtr && metadata));
  MOCK_METHOD(void, onRemoteClose, (Grpc::Status::GrpcStatus status, const std::string& message));
  MOCK_METHOD(void, closeStream, ());
};

} // namespace Config
} // namespace Envoy
