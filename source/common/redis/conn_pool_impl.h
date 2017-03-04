#pragma once

#include "envoy/upstream/cluster_manager.h"
#include "envoy/redis/conn_pool.h"
#include "envoy/thread_local/thread_local.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/filter_impl.h"
#include "common/redis/codec_impl.h"

namespace Redis {
namespace ConnPool {

// TODO(mattklein123): Stats
// TODO(mattklein123): Connect timeout
// TODO(mattklein123): Op timeout
// TODO(mattklein123): Circuit breaking

class ClientImpl : public Client, public DecoderCallbacks, public Network::ConnectionCallbacks {
public:
  static ClientPtr create(Upstream::ConstHostPtr host, Event::Dispatcher& dispatcher,
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
  ClientPtr create(Upstream::ConstHostPtr host, Event::Dispatcher& dispatcher) override;

  static ClientFactoryImpl instance_;

private:
  DecoderFactoryImpl decoder_factory_;
};

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
    void onEvent(uint32_t events) override;

    ThreadLocalPool& parent_;
    Upstream::ConstHostPtr host_;
    ClientPtr redis_client_;
  };

  typedef std::unique_ptr<ThreadLocalActiveClient> ThreadLocalActiveClientPtr;

  struct ThreadLocalPool : public ThreadLocal::ThreadLocalObject {
    ThreadLocalPool(InstanceImpl& parent, Event::Dispatcher& dispatcher,
                    const std::string& cluster_name);

    ActiveRequest* makeRequest(const std::string& hash_key, const RespValue& request,
                               ActiveRequestCallbacks& callbacks);
    void onHostsRemoved(const std::vector<Upstream::HostPtr>& hosts_removed);

    // ThreadLocal::ThreadLocalObject
    void shutdown() override;

    InstanceImpl& parent_;
    Event::Dispatcher& dispatcher_;
    Upstream::ThreadLocalCluster* cluster_;
    std::unordered_map<Upstream::ConstHostPtr, ThreadLocalActiveClientPtr> client_map_;
  };

  struct LbContextImpl : public Upstream::LoadBalancerContext {
    LbContextImpl(const std::string& hash_key) : hash_key_(std::hash<std::string>()(hash_key)) {}

    // Upstream::LoadBalancerContext
    const Optional<uint64_t>& hashKey() const override { return hash_key_; }

    const Optional<uint64_t> hash_key_;
  };

  Upstream::ClusterManager& cm_;
  ClientFactory& client_factory_;
  ThreadLocal::Instance& tls_;
  uint32_t tls_slot_;
};

} // ConnPool
} // Redis
