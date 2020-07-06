#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "extensions/common/redis/cluster_refresh_manager.h"
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
  MockRouter(RouteSharedPtr route);
  ~MockRouter() override;

  MOCK_METHOD(RouteSharedPtr, upstreamPool, (std::string & key));
  RouteSharedPtr route_;
};

class MockRoute : public Route {
public:
  MockRoute(ConnPool::InstanceSharedPtr);
  ~MockRoute() override;

  MOCK_METHOD(ConnPool::InstanceSharedPtr, upstream, (), (const));
  MOCK_METHOD(const MirrorPolicies&, mirrorPolicies, (), (const));
  ConnPool::InstanceSharedPtr conn_pool_;
  MirrorPolicies policies_;
};

class MockMirrorPolicy : public MirrorPolicy {
public:
  MockMirrorPolicy(ConnPool::InstanceSharedPtr);
  ~MockMirrorPolicy() override = default;

  MOCK_METHOD(ConnPool::InstanceSharedPtr, upstream, (), (const));
  MOCK_METHOD(bool, shouldMirror, (const std::string&), (const));

  ConnPool::InstanceSharedPtr conn_pool_;
};

namespace ConnPool {

class MockPoolCallbacks : public PoolCallbacks {
public:
  MockPoolCallbacks();
  ~MockPoolCallbacks() override;

  void onResponse(Common::Redis::RespValuePtr&& value) override { onResponse_(value); }
  void onFailure() override { onFailure_(); }

  MOCK_METHOD(void, onResponse_, (Common::Redis::RespValuePtr & value));
  MOCK_METHOD(void, onFailure_, ());
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

  MOCK_METHOD(Common::Redis::Client::PoolRequest*, makeRequest_,
              (const std::string& hash_key, RespVariant& request, PoolCallbacks& callbacks));
  MOCK_METHOD(bool, onRedirection, ());
};
} // namespace ConnPool

namespace CommandSplitter {

class MockSplitRequest : public SplitRequest {
public:
  MockSplitRequest();
  ~MockSplitRequest() override;

  MOCK_METHOD(void, cancel, ());
};

class MockSplitCallbacks : public SplitCallbacks {
public:
  MockSplitCallbacks();
  ~MockSplitCallbacks() override;

  void onResponse(Common::Redis::RespValuePtr&& value) override { onResponse_(value); }

  MOCK_METHOD(bool, connectionAllowed, ());
  MOCK_METHOD(void, onAuth, (const std::string& password));
  MOCK_METHOD(void, onAuth, (const std::string& username, const std::string& password));
  MOCK_METHOD(void, onResponse_, (Common::Redis::RespValuePtr & value));
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  SplitRequestPtr makeRequest(Common::Redis::RespValuePtr&& request,
                              SplitCallbacks& callbacks) override {
    return SplitRequestPtr{makeRequest_(*request, callbacks)};
  }

  MOCK_METHOD(SplitRequest*, makeRequest_,
              (const Common::Redis::RespValue& request, SplitCallbacks& callbacks));
};

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
