#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "envoy/stream_info/stream_info.h"

#include "source/extensions/common/redis/cluster_refresh_manager.h"
#include "source/extensions/filters/network/common/redis/client.h"
#include "source/extensions/filters/network/common/redis/codec_impl.h"
#include "source/extensions/filters/network/common/redis/fault.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter.h"
#include "source/extensions/filters/network/redis_proxy/conn_pool.h"
#include "source/extensions/filters/network/redis_proxy/router.h"

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

  MOCK_METHOD(RouteSharedPtr, upstreamPool,
              (std::string & key, const StreamInfo::StreamInfo& stream_info));
  MOCK_METHOD(void, initializeReadFilterCallbacks, (Network::ReadFilterCallbacks * callbacks));
  RouteSharedPtr route_;
};

class MockRoute : public Route {
public:
  MockRoute(ConnPool::InstanceSharedPtr);
  ~MockRoute() override;

  MOCK_METHOD(ConnPool::InstanceSharedPtr, upstream, (const std::string&), (const));
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

class MockFaultManager : public Common::Redis::FaultManager {
public:
  MockFaultManager();
  MockFaultManager(const MockFaultManager& other);
  ~MockFaultManager() override;

  MOCK_METHOD(const Common::Redis::Fault*, getFaultForCommand, (const std::string&), (const));
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
                                                  RespVariant&& request, PoolCallbacks& callbacks,
                                                  Common::Redis::Client::Transaction&) override {
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
  Common::Redis::Client::Transaction& transaction() override { return transaction_; }

  MOCK_METHOD(bool, connectionAllowed, ());
  MOCK_METHOD(void, onQuit, ());
  MOCK_METHOD(void, onAuth, (const std::string& password));
  MOCK_METHOD(void, onAuth, (const std::string& username, const std::string& password));
  MOCK_METHOD(void, onResponse_, (Common::Redis::RespValuePtr & value));

private:
  Common::Redis::Client::NoOpTransaction transaction_;
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  SplitRequestPtr makeRequest(Common::Redis::RespValuePtr&& request, SplitCallbacks& callbacks,
                              Event::Dispatcher& dispatcher,
                              const StreamInfo::StreamInfo& stream_info) override {
    return SplitRequestPtr{makeRequest_(*request, callbacks, dispatcher, stream_info)};
  }
  MOCK_METHOD(SplitRequest*, makeRequest_,
              (const Common::Redis::RespValue& request, SplitCallbacks& callbacks,
               Event::Dispatcher& dispatcher, const StreamInfo::StreamInfo& stream_info));
};

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
