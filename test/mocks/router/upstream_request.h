#pragma once

#include "source/common/router/upstream_request.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Router {

class MockUpstreamRequest : public UpstreamRequest {
public:
  MockUpstreamRequest(RouterFilterInterface& router_interface, std::unique_ptr<GenericConnPool>&&);
  ~MockUpstreamRequest() override;
  MOCK_METHOD(void, acceptHeadersFromRouter, (bool end_stream), (override));
  MOCK_METHOD(void, acceptDataFromRouter, (Buffer::Instance & data, bool end_stream), (override));
};

} // namespace Router
} // namespace Envoy
