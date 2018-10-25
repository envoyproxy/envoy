#pragma once

#include "extensions/filters/network/dubbo_proxy/protocol.h"

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

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy