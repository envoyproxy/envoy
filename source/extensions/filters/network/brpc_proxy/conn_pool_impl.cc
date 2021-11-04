#include "envoy/event/dispatcher.h"

#include "source/extensions/filters/network/brpc_proxy/conn_pool_impl.h"
#include "source/extensions/filters/network/brpc_proxy/client_impl.h"
namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {

ConnInstanceImpl::ConnInstanceImpl(std::string cluster,
			Upstream::ClusterManager& cluster_manager,
			ClientFactory& client_factory, 
			ThreadLocal::SlotAllocator& tls):
			cluster_(cluster),
			cm_(cluster_manager),
			client_factory_(client_factory),
			tls_(tls.allocateSlot()){
}
void ConnInstanceImpl::init(){
	std::weak_ptr<ConnInstanceImpl> this_weak_ptr = this->shared_from_this();
	tls_->set(
		[this_weak_ptr](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
		 if (auto this_shared_ptr = this_weak_ptr.lock()) {
		    return std::make_shared<ClientWrapper>(this_shared_ptr->client_factory_,dispatcher,this_shared_ptr->cm_.getThreadLocalCluster(this_shared_ptr->cluster_));
		 }
		 return nullptr;
	});
}
BrpcRequest* ConnInstanceImpl::makeRequest(BrpcMessagePtr&& request, PoolCallbacks& callbacks) {
	return tls_->getTyped<ClientWrapper>().makeRequest(std::move(request), callbacks);

}

} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

