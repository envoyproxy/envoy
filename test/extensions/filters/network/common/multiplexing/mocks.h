#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "extensions/filters/network/common/redis/client.h"
#include "extensions/filters/network/common/redis/codec_impl.h"
#include "extensions/filters/network/common/multiplexing/conn_pool.h"
#include "extensions/filters/network/common/multiplexing/router.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Multiplexing {

class MockRouter : public Router {
public:
  MockRouter();
  ~MockRouter();

  MOCK_METHOD1(upstreamPool, Common::Multiplexing::ConnPool::InstanceSharedPtr(std::string& key));
};

namespace ConnPool {

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  MOCK_METHOD3(makeRequest,
               Common::Redis::Client::PoolRequest*(
                   const std::string& hash_key, const Common::Redis::RespValue& request,
                   Common::Redis::Client::PoolCallbacks& callbacks));
  MOCK_METHOD3(makeRequestToHost,
               Common::Redis::Client::PoolRequest*(
                   const std::string& host_address, const Common::Redis::RespValue& request,
                   Common::Redis::Client::PoolCallbacks& callbacks));
};

} // namespace ConnPool
} // namespace Multiplexing
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
