#pragma once

#include "envoy/upstream/load_balancer.h"

#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockLoadBalancer : public LoadBalancer {
public:
  MockLoadBalancer();
  ~MockLoadBalancer() override;

  // Upstream::LoadBalancer
  MOCK_METHOD(HostConstSharedPtr, chooseHost, (LoadBalancerContext * context));
  MOCK_METHOD(HostConstSharedPtr, peekAnotherHost, (LoadBalancerContext * context));
  MOCK_METHOD(absl::optional<Upstream::SelectedPoolAndConnection>, selectExistingConnection,
              (Upstream::LoadBalancerContext * context, const Upstream::Host& host,
               std::vector<uint8_t>& hash_key));
  MOCK_METHOD(OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks>, lifetimeCallbacks,
              ());

  std::shared_ptr<MockHost> host_{new MockHost()};
};

class MockLoadBalancerFactoryContext : public LoadBalancerFactoryContext {
public:
  MockLoadBalancerFactoryContext() = default;
  ~MockLoadBalancerFactoryContext() = default;
  MOCK_METHOD(Event::Dispatcher&, mainThreadDispatcher, ());
};

} // namespace Upstream
} // namespace Envoy
