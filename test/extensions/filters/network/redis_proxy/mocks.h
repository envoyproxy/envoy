#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "extensions/filters/network/common/redis/client.h"
#include "extensions/filters/network/common/redis/codec_impl.h"
#include "extensions/filters/network/redis_proxy/command_splitter.h"
#include "extensions/filters/network/redis_proxy/conn_pool.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

namespace ConnPool {

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  MOCK_METHOD3(makeRequest,
               Common::Redis::Client::PoolRequest*(
                   const std::string& hash_key, const Common::Redis::RespValue& request,
                   Common::Redis::Client::PoolCallbacks& callbacks));
};

} // namespace ConnPool

namespace CommandSplitter {

class MockSplitRequest : public SplitRequest {
public:
  MockSplitRequest();
  ~MockSplitRequest();

  MOCK_METHOD0(cancel, void());
};

class MockSplitCallbacks : public SplitCallbacks {
public:
  MockSplitCallbacks();
  ~MockSplitCallbacks();

  void onResponse(Common::Redis::RespValuePtr&& value) override { onResponse_(value); }

  MOCK_METHOD1(onResponse_, void(Common::Redis::RespValuePtr& value));
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  SplitRequestPtr makeRequest(const Common::Redis::RespValue& request,
                              SplitCallbacks& callbacks) override {
    return SplitRequestPtr{makeRequest_(request, callbacks)};
  }

  MOCK_METHOD2(makeRequest_,
               SplitRequest*(const Common::Redis::RespValue& request, SplitCallbacks& callbacks));
};

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
