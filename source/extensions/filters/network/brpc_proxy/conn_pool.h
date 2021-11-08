#pragma once


#include "extensions/filters/network/brpc_proxy/client.h"
#include "extensions/filters/network/brpc_proxy/codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {

/**
 * A brpc connection pool. Wraps 1 connections to a upstream hosts, failure handling, etc.
 */
class ConnInstance {
public:
  virtual ~ConnInstance() = default;

  /**
   * Makes a brpc request.
   * @param request supplies the request to make.
   * @param callbacks supplies the request completion callbacks.
   * @return PoolRequest* a handle to the active request or nullptr if the request could not be made
   *         for some reason.
   */
  virtual BrpcRequest*
  makeRequest(BrpcMessagePtr&& request, PoolCallbacks& callbacks) PURE;
};

using ConnInstanceSharedPtr = std::shared_ptr<ConnInstance>;

} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

