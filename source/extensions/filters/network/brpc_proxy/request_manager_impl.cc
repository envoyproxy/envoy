#include "common/common/assert.h"
#include "extensions/filters/network/brpc_proxy/request_manager_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {
namespace RequestManager {

BrpcRequestImpl::~BrpcRequestImpl() {ASSERT(!handler_);}


void BrpcRequestImpl::onResponse(BrpcMessagePtr&& value) {
	handler_ = nullptr;
	callbacks_.onResponse(std::move(value));
	return;
}

BrpcRequestPtr RMInstanceImpl::makeRequest(BrpcMessagePtr&& value, 
										PoolCallbacks& callbacks, 
										Event::Dispatcher&/* no use now*/) {
	std::unique_ptr<BrpcRequestImpl> request_ptr{new BrpcRequestImpl(callbacks)};
	ConnInstanceSharedPtr connPoolIns =  upstreams_[value->service_name()];
	request_ptr->set_handler(connPoolIns->makeRequest(std::move(value),*request_ptr));
	if(!request_ptr->has_handler()) {
		BrpcMessagePtr err(new BrpcMessage(500, "internal error", value->request_id()));
		callbacks.onResponse(std::move(err));
		return nullptr;
	}
	return request_ptr;
}
} // namespace RequestManager
} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

