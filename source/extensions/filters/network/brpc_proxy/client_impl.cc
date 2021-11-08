#include "extensions/filters/network/brpc_proxy/client_impl.h"
#include "envoy/extensions/filters/network/brpc_proxy/v3/brpc_proxy.pb.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {
//todo add timeout timer
ClientPtr ClientImpl::create(Upstream::HostConstSharedPtr host, 
							Event::Dispatcher& dispatcher,
							EncoderPtr&& encoder,
							DecoderFactory& decoder_factory){
	auto client = std::make_unique<ClientImpl>(host,std::move(encoder), decoder_factory);
	client->connection_ = host->createConnection(dispatcher, nullptr, nullptr).connection_;
	client->connection_->addConnectionCallbacks(*client);
	client->connection_->addReadFilter(Network::ReadFilterSharedPtr{new UpstreamReadFilter(*client)});
	client->connection_->connect();
	client->connection_->noDelay(true);
	return client;
}

ClientImpl::ClientImpl(Upstream::HostConstSharedPtr host,
                       EncoderPtr&& encoder, DecoderFactory& decoder_factory)
    : host_(host), encoder_(std::move(encoder)), decoder_(decoder_factory.create(*this)) {
}

ClientImpl::~ClientImpl() {
	auto it = pending_requests_.begin();
	while(it != pending_requests_.end()){
		PendingRequestPtr& request = it->second;
		if (!request->canceled_) {
			request->callbacks_.onResponse(BrpcMessagePtr {new BrpcMessage(500, "connection closed", it->first)});
		} else {
			host_->cluster().stats().upstream_rq_cancelled_.inc();
		}
		it++;
	}
	pending_requests_.clear();
	close();

	//ASSERT(pending_requests_.empty());
	//ASSERT(connection_->state() == Network::Connection::State::Closed);
}

void ClientImpl::close() { connection_->close(Network::ConnectionCloseType::NoFlush); }


BrpcRequest* ClientImpl::makeRequest(BrpcMessage& request, PoolCallbacks& callbacks) {
	ASSERT(connection_->state() == Network::Connection::State::Open);
	pending_requests_.emplace(request.request_id(),PendingRequestPtr{new PendingRequest(*this, callbacks)});
	encoder_->encode(request, encoder_buffer_);
	if(connected_){
		connection_->write(encoder_buffer_, false);
	}
	return pending_requests_[request.request_id()].get();
}

void ClientImpl::onData(Buffer::Instance& data) {
	try {
	decoder_->decode(data);
	} catch (ProtocolError&) {
	connection_->close(Network::ConnectionCloseType::NoFlush);
	}
}

void ClientImpl::onEvent(Network::ConnectionEvent event) {
	if (event == Network::ConnectionEvent::RemoteClose ||
	  event == Network::ConnectionEvent::LocalClose) {

		if (!pending_requests_.empty()) {
		  auto it = pending_requests_.begin();
		  while(it != pending_requests_.end()){
		  	PendingRequestPtr& request = it->second;
			if (!request->canceled_) {
				request->callbacks_.onResponse(BrpcMessagePtr {new BrpcMessage(500, "connection closed", it->first)});
			} else {
				host_->cluster().stats().upstream_rq_cancelled_.inc();
			}
			it++;
		  }
		  pending_requests_.clear();
		}
	} else if (event == Network::ConnectionEvent::Connected) {
		connected_ = true;
		ASSERT(!pending_requests_.empty());
		connection_->write(encoder_buffer_, false);
	}

	if (event == Network::ConnectionEvent::RemoteClose && !connected_) {
		host_->cluster().stats().upstream_cx_connect_fail_.inc();
		host_->stats().cx_connect_fail_.inc();
	}
}

void ClientImpl::onMessage(BrpcMessagePtr&& value) {
  ASSERT(!pending_requests_.empty());
  auto it = pending_requests_.find(value->request_id());
  if(it == pending_requests_.end()){
		ENVOY_LOG(error,"request id not found in pending requests");
		return;
  }
  PendingRequestPtr& request = it->second;
  const bool canceled = request->canceled_;
  PoolCallbacks& callbacks = request->callbacks_;

  // We need to ensure the request is popped before calling the callback, since the callback might
  // result in closing the connection.
  pending_requests_.erase(value->request_id());
  if (canceled) {
    host_->cluster().stats().upstream_rq_cancelled_.inc();
  } else {
    callbacks.onResponse(std::move(value));
  }
}

ClientImpl::PendingRequest::PendingRequest(ClientImpl& parent, PoolCallbacks& callbacks)
    : parent_(parent), callbacks_(callbacks){
}

ClientImpl::PendingRequest::~PendingRequest() {
}

void ClientImpl::PendingRequest::cancel() {
  // If we get a cancellation, we just mark the pending request as cancelled, and then we drop
  // the response as it comes through. There is no reason to blow away the connection when the
  // remote is already responding as fast as possible.
  canceled_ = true;
}

ClientFactoryImpl ClientFactoryImpl::instance_;

ClientPtr ClientFactoryImpl::create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher) {
  ClientPtr client = ClientImpl::create(host, dispatcher, EncoderPtr{new EncoderImpl()},decoder_factory_);
  return client;
}


ClientPtr& ClientWrapper::getClient(Upstream::HostConstSharedPtr host) {
	ClientPtr& client = client_map_[host];
	if(!client){
		client = client_factory_.create(host, dispatcher_);	
	}
	return client;
}

BrpcRequest* ClientWrapper::makeRequest(BrpcMessagePtr && request, PoolCallbacks & callbacks) {
	if(!cluster_) {
		ENVOY_LOG(error, "cluster not found");
		return nullptr;
	}
	Upstream::HostConstSharedPtr host = cluster_->loadBalancer().chooseHost(NULL);
	if (!host) {
		ENVOY_LOG(error, "host not found");
		return nullptr;
	}
	//todo add callback
	ClientPtr& cli = getClient(host);
	return cli->makeRequest(*request, callbacks);
}


} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
