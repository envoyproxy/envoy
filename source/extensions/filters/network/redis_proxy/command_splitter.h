#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "extensions/filters/network/redis_proxy/codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace CommandSplitter {

/**
 * A handle to a split request.
 */
class SplitRequest {
public:
  virtual ~SplitRequest() {}

  /**
   * Cancel the request. No further request callbacks will be called.
   */
  virtual void cancel() PURE;
};

typedef std::unique_ptr<SplitRequest> SplitRequestPtr;

/**
 * Split request callbacks.
 */
class SplitCallbacks {
public:
  virtual ~SplitCallbacks() {}

  /**
   * Called when the response is ready.
   * @param value supplies the response which is now owned by the callee.
   */
  virtual void onResponse(RespValuePtr&& value) PURE;
};

/**
 * A command splitter that takes incoming redis commands and splits them as appropriate to a
 * backend connection pool.
 */
class Instance {
public:
  virtual ~Instance() {}

  /**
   * Make a split redis request.
   * @param request supplies the split request to make.
   * @param callbacks supplies the split request completion callbacks.
   * @return SplitRequestPtr a handle to the active request or nullptr if the request has already
   *         been satisfied (via onResponse() being called). The splitter ALWAYS calls
   *         onResponse() for a given request.
   */
  virtual SplitRequestPtr makeRequest(const RespValue& request, SplitCallbacks& callbacks) PURE;
};

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
