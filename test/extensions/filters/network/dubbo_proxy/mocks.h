#pragma once

#include "extensions/filters/network/dubbo_proxy/protocol.h"
#include "extensions/filters/network/dubbo_proxy/deserializer.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class MockProtocolCallbacks : public ProtocolCallbacks {
public:
  MockProtocolCallbacks();
  ~MockProtocolCallbacks();

  void onRequestMessage(RequestMessagePtr&& req) override { onRequestMessageRvr(req.get()); }
  void onResponseMessage(ResponseMessagePtr&& res) override { onResponseMessageRvr(res.get()); }

  // DubboProxy::ProtocolCallbacks
  MOCK_METHOD1(onRequestMessageRvr, void(RequestMessage*));
  MOCK_METHOD1(onResponseMessageRvr, void(ResponseMessage*));
};

class MockProtocol : public Protocol {
public:
  MockProtocol();
  ~MockProtocol();

  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_METHOD2(decode, bool(Buffer::Instance&, Context*));
  std::string name_{"MockProtocol"};
};

class MockDeserializer : public Deserializer {
public:
  MockDeserializer();
  ~MockDeserializer();
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_METHOD2(deserializeRpcInvocation, void(Buffer::Instance&, size_t));
  MOCK_METHOD2(deserializeRpcResult, void(Buffer::Instance&, size_t));
  std::string name_{"MockSerialization"};
};

class MockDeserializationCallbacks : public DeserializationCallbacks {
public:
  MockDeserializationCallbacks();
  ~MockDeserializationCallbacks();

  void onRpcInvocation(RpcInvocationPtr&& invo) override { onRpcInvocationRvr(invo.get()); }
  void onRpcResult(RpcResultPtr&& res) override { onRpcResultRvr(res.get()); }

  // DubboProxy::SerializationCallbacks
  MOCK_METHOD1(onRpcInvocationRvr, void(RpcInvocation*));
  MOCK_METHOD1(onRpcResultRvr, void(RpcResult*));
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy