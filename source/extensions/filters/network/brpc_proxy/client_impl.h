#pragma once

#include <cstdint>
#include <map>

#include "absl/container/node_hash_map.h"
#include "envoy/thread_local/thread_local_object.h"
#include "envoy/upstream/thread_local_cluster.h"
#include "source/common/network/filter_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/network/brpc_proxy/client.h"
#include "source/extensions/filters/network/brpc_proxy/codec_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {

class ClientImpl :  public ThreadLocal::ThreadLocalObject,
		    public Client, 
		    public DecoderCallbacks, 
		    public Network::ConnectionCallbacks,
           	    public Logger::Loggable<Logger::Id::brpc> {
public:
  static ClientPtr create(Upstream::HostConstSharedPtr host,Event::Dispatcher& dispatcher,
						  EncoderPtr&& encoder, DecoderFactory& decoder_factory);

  ClientImpl(Upstream::HostConstSharedPtr host, EncoderPtr&& encoder,
			 DecoderFactory& decoder_factory);
  ~ClientImpl() override;

  // Client
  void addConnectionCallbacks(Network::ConnectionCallbacks& callbacks) override {
	connection_->addConnectionCallbacks(callbacks);
  }
  void close() override;
  BrpcRequest* makeRequest(BrpcMessage& request, PoolCallbacks& callbacks) override;
  bool active() override { return !pending_requests_.empty(); }
private:
  struct UpstreamReadFilter : public Network::ReadFilterBaseImpl {
	UpstreamReadFilter(ClientImpl& parent) : parent_(parent) {}

	// Network::ReadFilter
	Network::FilterStatus onData(Buffer::Instance& data, bool) override {
	  parent_.onData(data);
	  return Network::FilterStatus::Continue;
	}

	ClientImpl& parent_;
  };

  struct PendingRequest : public BrpcRequest {
	PendingRequest(ClientImpl& parent, PoolCallbacks& callbacks);
	~PendingRequest() override;

	// BrpcRequest
	void cancel() override;

	ClientImpl& parent_;
	PoolCallbacks& callbacks_;
	bool canceled_{};
  };
  using PendingRequestPtr = std::unique_ptr<PendingRequest>;
  void onData(Buffer::Instance& data);

  // DecoderCallbacks
  void onMessage(BrpcMessagePtr&& value) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  Upstream::HostConstSharedPtr host_;
  //Event::Dispatcher& dispatcher_;
  Network::ClientConnectionPtr connection_;
  EncoderPtr encoder_;
  Buffer::OwnedImpl encoder_buffer_;
  DecoderPtr decoder_;
  std::map<int64_t, PendingRequestPtr> pending_requests_;
  bool connected_{};
};

class ClientFactoryImpl : public ClientFactory {
public:
  ClientPtr create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher) override;

  static ClientFactoryImpl instance_;

private:
  DecoderFactoryImpl decoder_factory_;
};

class ClientWrapper:public ThreadLocal::ThreadLocalObject,
		public Logger::Loggable<Logger::Id::brpc> {
public:
	ClientWrapper(ClientFactory& client_factory, Event::Dispatcher& dispatcher,
				Upstream::ThreadLocalCluster* cluster):
		client_factory_(client_factory), dispatcher_(dispatcher),cluster_(cluster){}
	BrpcRequest* makeRequest(BrpcMessagePtr&& request, PoolCallbacks& callbacks);	
private:
	ClientPtr& getClient(Upstream::HostConstSharedPtr host);
	ClientFactory& client_factory_;
	Event::Dispatcher& dispatcher_;
	Upstream::ThreadLocalCluster* cluster_{};
	absl::node_hash_map<Upstream::HostConstSharedPtr, ClientPtr> client_map_;
};
} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

