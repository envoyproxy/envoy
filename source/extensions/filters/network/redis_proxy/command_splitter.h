#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "extensions/filters/network/common/redis/codec.h"

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
  virtual ~SplitRequest() = default;

  /**
   * Cancel the request. No further request callbacks will be called.
   */
  virtual void cancel() PURE;
};

using SplitRequestPtr = std::unique_ptr<SplitRequest>;

/**
 * Split request callbacks.
 */
class SplitCallbacks {
public:
  virtual ~SplitCallbacks() = default;

  /**
   * Called to verify that commands should be processed.
   * @return bool true if commands from this client connection can be processed, false if not.
   */
  virtual bool connectionAllowed() PURE;

  /**
   * Called when an authentication command has been received.
   * @param password supplies the AUTH password provided by the downstream client.
   */
  virtual void onAuth(const std::string& password) PURE;

  /**
   * Called when the response is ready.
   * @param value supplies the response which is now owned by the callee.
   */
  virtual void onResponse(Common::Redis::RespValuePtr&& value) PURE;
};

/**
 * A command splitter that takes incoming redis commands and splits them as appropriate to a
 * backend connection pool.
 */
class Instance {
public:
  virtual ~Instance() = default;

  /**
   * Make a split redis request capable of being retried/redirected.
   * @param request supplies the split request to make (ownership transferred to call).
   * @param callbacks supplies the split request completion callbacks.
   * @return SplitRequestPtr a handle to the active request or nullptr if the request has already
   *         been satisfied (via onResponse() being called). The splitter ALWAYS calls
   *         onResponse() for a given request.
   */
  virtual SplitRequestPtr makeRequest(Common::Redis::RespValuePtr&& request,
                                      SplitCallbacks& callbacks) PURE;
};

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
