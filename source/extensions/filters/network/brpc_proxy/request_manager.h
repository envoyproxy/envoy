#pragma once

#include "source/extensions/filters/network/brpc_proxy/conn_pool.h"
namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {
namespace RequestManager {


class RMInstance {
public:
	virtual ~RMInstance() = default;
	virtual BrpcRequestPtr makeRequest(BrpcMessagePtr&& value, PoolCallbacks& callbacks, Event::Dispatcher& dispatcher) PURE;
};

using RMInstanceSharedPtr = std::shared_ptr<RMInstance>;

} // namespace RequestManager
} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy


