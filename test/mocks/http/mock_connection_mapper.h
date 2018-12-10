#pragma once

#include <vector>

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

  void addIdleCallback(ConnectionMapper::IdleCb cb) override {
    idle_callbacks_.push_back(cb);
    addIdleCallback_();
  }

  MOCK_METHOD1(assignPool_,
               ConnectionPool::Instance*(const Upstream::LoadBalancerContext* context));
  MOCK_METHOD0(addIdleCallback_, void());
  MOCK_METHOD0(drainPools, void());
  MOCK_CONST_METHOD0(allPoolsIdle, bool());

  /**
   * May be used to track the builder passed to a mapper on construction.
   */
  ConnPoolBuilder builder_;
  std::vector<ConnectionMapper::IdleCb> idle_callbacks_;
};

} // namespace Http
} // namespace Envoy
