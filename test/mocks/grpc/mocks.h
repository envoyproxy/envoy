#pragma once

#include "envoy/grpc/rpc_channel.h"

namespace Grpc {

class MockRpcChannelCallbacks : public RpcChannelCallbacks {
public:
  MockRpcChannelCallbacks();
  ~MockRpcChannelCallbacks();

  MOCK_METHOD0(onSuccess, void());
  MOCK_METHOD2(onFailure, void(const Optional<uint64_t>& grpc_status, const std::string& message));
};

class MockRpcChannel : public RpcChannel {
public:
  MockRpcChannel();
  ~MockRpcChannel();

  MOCK_METHOD0(cancel, void());
  MOCK_METHOD5(CallMethod,
               void(const proto::MethodDescriptor* method, proto::RpcController* controller,
                    const proto::Message* request, proto::Message* response, proto::Closure* done));
};

} // Grpc

MATCHER_P(ProtoMessageEqual, rhs, "") {
  return arg->SerializeAsString() == rhs->SerializeAsString();
}
