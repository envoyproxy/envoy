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

  MOCK_METHOD(void, establishNewStream, (), (override));
  MOCK_METHOD(bool, grpcStreamAvailable, (), (const, override));
  MOCK_METHOD(void, sendMessage, (const RequestProto& request), (override));
  MOCK_METHOD(void, maybeUpdateQueueSizeStat, (uint64_t size), (override));
  MOCK_METHOD(bool, checkRateLimitAllowsDrain, (), (override));
  MOCK_METHOD(void, onCreateInitialMetadata, (Http::RequestHeaderMap & metadata), (override));
  MOCK_METHOD(void, onReceiveInitialMetadata, (Http::ResponseHeaderMapPtr && metadata), (override));
  MOCK_METHOD(void, onReceiveMessage, (ResponseProtoPtr<ResponseProto> && message), (override));
  MOCK_METHOD(void, onReceiveTrailingMetadata, (Http::ResponseTrailerMapPtr && metadata),
              (override));
  MOCK_METHOD(void, onRemoteClose, (Grpc::Status::GrpcStatus status, const std::string& message),
              (override));
  MOCK_METHOD(void, closeStream, (), (override));
};

} // namespace Config
} // namespace Envoy
