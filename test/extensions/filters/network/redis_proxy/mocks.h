#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "extensions/filters/network/common/redis/client.h"
#include "extensions/filters/network/common/redis/codec_impl.h"
#include "extensions/filters/network/redis_proxy/command_splitter.h"
#include "extensions/filters/network/redis_proxy/conn_pool.h"
#include "extensions/filters/network/redis_proxy/redirection_mgr.h"
#include "extensions/filters/network/redis_proxy/router.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

class MockRouter : public Router {
public:
  MockRouter() = default;
  ~MockRouter() = default;

  MOCK_METHOD1(upstreamPool, RouteSharedPtr(std::string& key));
};

class MockRoute : public Route {
public:
  MockRoute(ConnPool::InstanceSharedPtr);
  ~MockRoute() = default;
  MOCK_CONST_METHOD0(upstream, ConnPool::InstanceSharedPtr());
  ConnPool::InstanceSharedPtr conn_pool_;
  const MirrorPolicies policies_;
};

class MockRedirectionManager : public RedirectionManager {
public:
  MockRedirectionManager() = default;
  ~MockRedirectionManager() = default;

  MOCK_METHOD1(onRedirection, bool(const std::string& cluster_name));
  MOCK_METHOD4(registerCluster,
               HandlePtr(const std::string& cluster_name,
                         const std::chrono::milliseconds min_time_between_triggering,
                         const uint32_t redirects_per_minute_threshold, const RedirectCB cb));
  MOCK_METHOD1(unregisterCluster, void(const std::string& cluster_name));
};

namespace ConnPool {

class MockInstance : public Instance {
public:
  MockInstance() = default;
  ~MockInstance() = default;

  MOCK_METHOD3(makeRequest,
               Common::Redis::Client::PoolRequest*(
                   const std::string& hash_key, const Common::Redis::RespValue& request,
                   Common::Redis::Client::PoolCallbacks& callbacks));
  MOCK_METHOD3(makeRequestToHost,
               Common::Redis::Client::PoolRequest*(
                   const std::string& host_address, const Common::Redis::RespValue& request,
                   Common::Redis::Client::PoolCallbacks& callbacks));
  MOCK_METHOD0(onRedirection, bool());
};

} // namespace ConnPool

namespace CommandSplitter {

class MockSplitRequest : public SplitRequest {
public:
  MockSplitRequest() = default;
  ~MockSplitRequest() = default;

  MOCK_METHOD0(cancel, void());
};

class MockSplitCallbacks : public SplitCallbacks {
public:
  MockSplitCallbacks() = default;
  ~MockSplitCallbacks() = default;


  MOCK_METHOD1(onResponse_, void(Common::Redis::RespValuePtr& value));
};

class MockInstance : public Instance {
public:
  MockInstance() = default;
  ~MockInstance() = default;

  SplitRequestPtr makeRequest(Common::Redis::RespValuePtr&& request,
                              SplitCallbacks& callbacks) override {
    return SplitRequestPtr{makeRequest_(*request, callbacks)};
  }

  MOCK_METHOD2(makeRequest_,
               SplitRequest*(const Common::Redis::RespValue& request, SplitCallbacks& callbacks));
};

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
