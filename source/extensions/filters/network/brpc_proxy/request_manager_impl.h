#pragma once

#include <map>

#include "extensions/filters/network/brpc_proxy/request_manager.h"


namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {
namespace RequestManager {

using Upstreams = std::map<std::string, ConnInstanceSharedPtr>;


class BrpcRequestImpl:public BrpcRequest, public PoolCallbacks {
public:
	BrpcRequestImpl(PoolCallbacks& callback): callbacks_(callback){}
	~BrpcRequestImpl();
	//PoolCallbacks
	void onResponse(BrpcMessagePtr&& value) override;
	//BrpcRequest
	void cancel() override {handler_->cancel(); handler_ = nullptr;}
	void set_handler(BrpcRequest* handler){handler_ = handler;}
	bool has_handler(){return !!handler_;}
private:
	PoolCallbacks& callbacks_;
	BrpcRequest* handler_;
};

class RMInstanceImpl:public RMInstance {
public:
	RMInstanceImpl(Upstreams& upstreams):upstreams_(upstreams){}
	BrpcRequestPtr makeRequest(BrpcMessagePtr&& value, PoolCallbacks& callbacks, Event::Dispatcher& dispatcher) override;
private:
	Upstreams upstreams_;
};
} // namespace RequestManager
} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

