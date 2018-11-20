#pragma once

#include "envoy/http/connection_mapper.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockConnectionMapper : public ConnectionMapper {
public:
  MockConnectionMapper();
  ~MockConnectionMapper();
  ConnectionPool::Instance* assignPool(const Upstream::LoadBalancerContext& context) override {
    return assignPool_(&context);
  }

  MOCK_METHOD1(assignPool_,
               ConnectionPool::Instance*(const Upstream::LoadBalancerContext* context));
  MOCK_METHOD1(poolIdle, void(const Http::ConnectionPool::Instance& idlePool));

  ConnPoolBuilder builder_;
};

} // namespace Http
} // namespace Envoy
