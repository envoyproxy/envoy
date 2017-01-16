#pragma once

#include "envoy/upstream/cluster_manager.h"
#include "envoy/redis/conn_pool.h"
#include "envoy/thread_local/thread_local.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/filter_impl.h"
#include "common/redis/codec_impl.h"

namespace Redis {
namespace ConnPool {

// TODO: Stats
// TODO: Connect timeout
// TODO: Op timeout

class ClientImpl : public Client, public DecoderCallbacks, public Network::ConnectionCallbacks {
public:
  static ClientPtr create(const std::string& cluster_name, Upstream::ClusterManager& cm,
                          EncoderPtr&& encoder, DecoderFactory& decoder_factory);

  ~ClientImpl();

  // Redis::ConnPool::Client
  void addConnectionCallbacks(Network::ConnectionCallbacks& callbacks) override {
    connection_->addConnectionCallbacks(callbacks);
  }
  void close() override;
  ActiveRequest* makeRequest(const RespValue& request, ActiveRequestCallbacks& callbacks) override;

private:
  struct UpstreamReadFilter : public Network::ReadFilterBaseImpl {
    UpstreamReadFilter(ClientImpl& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data) override {
      parent_.onData(data);
      return Network::FilterStatus::Continue;
    }

    ClientImpl& parent_;
  };

  struct PendingRequest : public ActiveRequest {
    PendingRequest(ActiveRequestCallbacks& callbacks) : callbacks_(callbacks) {}

    // Redis::ConnPool::ActiveRequest
    void cancel() override;

    ActiveRequestCallbacks& callbacks_;
    bool canceled_{};
  };

  ClientImpl(EncoderPtr&& encoder, DecoderFactory& decoder_factory)
      : encoder_(std::move(encoder)), decoder_(decoder_factory.create(*this)) {}

  void onData(Buffer::Instance& data);

  // Redis::DecoderCallbacks
  void onRespValue(RespValuePtr&& value) override;

  // Network::ConnectionCallbacks
  void onEvent(uint32_t events) override;

  Network::ClientConnectionPtr connection_;
  EncoderPtr encoder_;
  Buffer::OwnedImpl encoder_buffer_;
  DecoderPtr decoder_;
  std::list<PendingRequest> pending_requests_;
};

class ClientFactoryImpl : public ClientFactory {
public:
  // Redis::ConnPool::ClientFactoryImpl
  ClientPtr create(const std::string& cluster_name, Upstream::ClusterManager& cm) override;

  static ClientFactoryImpl instance_;

private:
  DecoderFactoryImpl decoder_factory_;
};

// TODO: Real hashing connection pool with N connections per upstream.

class InstanceImpl : public Instance {
public:
  InstanceImpl(const std::string& cluster_name, Upstream::ClusterManager& cm,
               ClientFactory& client_factory, ThreadLocal::Instance& tls);

  // Redis::ConnPool::Instance
  ActiveRequest* makeRequest(const std::string& hash_key, const RespValue& request,
                             ActiveRequestCallbacks& callbacks) override;

private:
  struct ThreadLocalPool;

  struct ThreadLocalActiveClient : public Network::ConnectionCallbacks,
                                   public Event::DeferredDeletable {
    ThreadLocalActiveClient(ThreadLocalPool& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(uint32_t events) override { parent_.onEvent(*this, events); }

    ThreadLocalPool& parent_;
    ClientPtr redis_client_;
  };

  typedef std::unique_ptr<ThreadLocalActiveClient> ThreadLocalActiveClientPtr;

  struct ThreadLocalPool : public ThreadLocal::ThreadLocalObject {
    ThreadLocalPool(InstanceImpl& parent, Event::Dispatcher& dispatcher)
        : parent_(parent), dispatcher_(dispatcher) {}

    ActiveRequest* makeRequest(const RespValue& request, ActiveRequestCallbacks& callbacks);
    void onEvent(ThreadLocalActiveClient& client, uint32_t events);

    // ThreadLocal::ThreadLocalObject
    void shutdown() override;

    InstanceImpl& parent_;
    Event::Dispatcher& dispatcher_;
    ThreadLocalActiveClientPtr client_;
  };

  const std::string cluster_name_;
  Upstream::ClusterManager& cm_;
  ClientFactory& client_factory_;
  ThreadLocal::Instance& tls_;
  uint32_t tls_slot_;
};

} // ConnPool
} // Redis
