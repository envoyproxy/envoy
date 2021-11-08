#pragma once

#include "envoy/upstream/cluster_manager.h"
#include "envoy/thread_local/thread_local.h"

#include "extensions/filters/network/brpc_proxy/conn_pool.h"
#include "extensions/filters/network/brpc_proxy/client.h"
namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {


class ConnInstanceImpl: public ConnInstance,
                        public std::enable_shared_from_this<ConnInstanceImpl> {
public:
	ConnInstanceImpl(std::string cluster,
				Upstream::ClusterManager& cluster_manager,
				ClientFactory& client_factory, 
				ThreadLocal::SlotAllocator& tls);
	BrpcRequest* makeRequest(BrpcMessagePtr&& request, PoolCallbacks& callbacks);
	void init();
private:
	std::string cluster_;
	Upstream::ClusterManager& cm_;
	ClientFactory& client_factory_;
	ThreadLocal::SlotPtr tls_;
};

} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy


