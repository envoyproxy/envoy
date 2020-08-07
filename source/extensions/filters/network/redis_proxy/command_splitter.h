#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"

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
   * Called when an authentication command has been received with a password.
   * @param password supplies the AUTH password provided by the downstream client.
   */
  virtual void onAuth(const std::string& password) PURE;

  /**
   * Called when an authentication command has been received with a username and password.
   * @param username supplies the AUTH username provided by the downstream client.
   * @param password supplies the AUTH password provided by the downstream client.
   */
  virtual void onAuth(const std::string& username, const std::string& password) PURE;

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

using CommandSplitterPtr = std::unique_ptr<Instance>;

/**
 * A command splitter factory that allows creation of the command splitter when
 * we have access to the dispatcher parameter. This supports fault injection,
 * specifically delay faults, which rely on the dispatcher for creating delay timers.
 */
class CommandSplitterFactory {
public:
  virtual ~CommandSplitterFactory() = default;

  /**
   * Create a command splitter.
   * @param dispatcher supplies the dispatcher .
   * @return CommandSplitterPtr a handle to a newly created command splitter.
   */
  virtual CommandSplitterPtr create(Event::Dispatcher& dispatcher) PURE;
};

using CommandSplitterFactorySharedPtr = std::shared_ptr<CommandSplitterFactory>;

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
