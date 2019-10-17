#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "extensions/filters/network/common/redis/client.h"
#include "extensions/filters/network/common/redis/codec_impl.h"
#include "extensions/filters/network/redis_proxy/command_splitter.h"
#include "extensions/filters/network/redis_proxy/conn_pool.h"
#include "extensions/filters/network/redis_proxy/router.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

class MockRouter : public Router {
public:
  MockRouter();
  ~MockRouter() override;

  MOCK_METHOD1(upstreamPool, RouteSharedPtr(std::string& key));
};

class MockRoute : public Route {
public:
  MockRoute(ConnPool::InstanceSharedPtr);
  ~MockRoute() override;

  MOCK_CONST_METHOD0(upstream, ConnPool::InstanceSharedPtr());
  MOCK_CONST_METHOD0(mirrorPolicies, const MirrorPolicies&());

private:
  ConnPool::InstanceSharedPtr conn_pool_;
  const MirrorPolicies policies_;
};

namespace ConnPool {

class MockPoolCallbacks : public PoolCallbacks {
public:
  MockPoolCallbacks();
  ~MockPoolCallbacks() override;

  void onResponse(Common::Redis::RespValuePtr&& value) override { onResponse_(value); }
  void onFailure() override { onFailure_(); }

  MOCK_METHOD1(onResponse_, void(Common::Redis::RespValuePtr& value));
  MOCK_METHOD0(onFailure_, void());
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  Common::Redis::Client::PoolRequest* makeRequest(const std::string& hash_key,
                                                  RespVariant&& request,
                                                  PoolCallbacks& callbacks) override {
    return makeRequest_(hash_key, request, callbacks);
  }

  MOCK_METHOD3(makeRequest_,
               Common::Redis::Client::PoolRequest*(const std::string& hash_key,
                                                   RespVariant& request, PoolCallbacks& callbacks));
};
} // namespace ConnPool

namespace CommandSplitter {

class MockSplitRequest : public SplitRequest {
public:
  MockSplitRequest();
  ~MockSplitRequest() override;

  MOCK_METHOD0(cancel, void());
};

class MockSplitCallbacks : public SplitCallbacks {
public:
  MockSplitCallbacks();
  ~MockSplitCallbacks() override;

  MOCK_METHOD0(connectionAllowed, bool());
  MOCK_METHOD1(onAuth, void(const std::string& password));

  void onResponse(Common::Redis::RespValuePtr&& value) override { onResponse_(value); }

  MOCK_METHOD1(onResponse_, void(Common::Redis::RespValuePtr& value));
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

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
