#pragma once

#include "envoy/redis/conn_pool.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"
#include "common/json/json_validator.h"
#include "common/network/filter_impl.h"
#include "common/redis/codec_impl.h"

namespace Redis {
namespace ConnPool {

// TODO(mattklein123): Circuit breaking

class ConfigImpl : public Config, Json::Validator {
public:
  ConfigImpl(const Json::Object& config);

  std::chrono::milliseconds opTimeout() const override { return op_timeout_; }

private:
  const std::chrono::milliseconds op_timeout_;
};

class ClientImpl : public Client, public DecoderCallbacks, public Network::ConnectionCallbacks {
public:
  static ClientPtr create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher,
                          EncoderPtr&& encoder, DecoderFactory& decoder_factory,
                          const Config& config);

  ~ClientImpl();

  // Redis::ConnPool::Client
  void addConnectionCallbacks(Network::ConnectionCallbacks& callbacks) override {
    connection_->addConnectionCallbacks(callbacks);
  }
  void close() override;
  PoolRequest* makeRequest(const RespValue& request, PoolCallbacks& callbacks) override;

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

  struct PendingRequest : public PoolRequest {
    PendingRequest(ClientImpl& parent, PoolCallbacks& callbacks);
    ~PendingRequest();

    // Redis::ConnPool::PoolRequest
    void cancel() override;

    ClientImpl& parent_;
    PoolCallbacks& callbacks_;
    bool canceled_{};
  };

  ClientImpl(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher, EncoderPtr&& encoder,
             DecoderFactory& decoder_factory, const Config& config);
  void onConnectOrOpTimeout();
  void onData(Buffer::Instance& data);

  // Redis::DecoderCallbacks
  void onRespValue(RespValuePtr&& value) override;

  // Network::ConnectionCallbacks
  void onEvent(uint32_t events) override;

  Upstream::HostConstSharedPtr host_;
  Network::ClientConnectionPtr connection_;
  EncoderPtr encoder_;
  Buffer::OwnedImpl encoder_buffer_;
  DecoderPtr decoder_;
  const Config& config_;
  std::list<PendingRequest> pending_requests_;
  Event::TimerPtr connect_or_op_timer_;
  bool connected_{};
};

class ClientFactoryImpl : public ClientFactory {
public:
  // Redis::ConnPool::ClientFactoryImpl
  ClientPtr create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher,
                   const Config& config) override;

  static ClientFactoryImpl instance_;

private:
  DecoderFactoryImpl decoder_factory_;
};

class InstanceImpl : public Instance {
public:
  InstanceImpl(const std::string& cluster_name, Upstream::ClusterManager& cm,
               ClientFactory& client_factory, ThreadLocal::Instance& tls,
               const Json::Object& config);

  // Redis::ConnPool::Instance
  PoolRequest* makeRequest(const std::string& hash_key, const RespValue& request,
                           PoolCallbacks& callbacks) override;

private:
  struct ThreadLocalPool;

  struct ThreadLocalActiveClient : public Network::ConnectionCallbacks,
                                   public Event::DeferredDeletable {
    ThreadLocalActiveClient(ThreadLocalPool& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(uint32_t events) override;

    ThreadLocalPool& parent_;
    Upstream::HostConstSharedPtr host_;
    ClientPtr redis_client_;
  };

  typedef std::unique_ptr<ThreadLocalActiveClient> ThreadLocalActiveClientPtr;

  struct ThreadLocalPool : public ThreadLocal::ThreadLocalObject {
    ThreadLocalPool(InstanceImpl& parent, Event::Dispatcher& dispatcher,
                    const std::string& cluster_name);

    PoolRequest* makeRequest(const std::string& hash_key, const RespValue& request,
                             PoolCallbacks& callbacks);
    void onHostsRemoved(const std::vector<Upstream::HostSharedPtr>& hosts_removed);

    // ThreadLocal::ThreadLocalObject
    void shutdown() override;

    InstanceImpl& parent_;
    Event::Dispatcher& dispatcher_;
    Upstream::ThreadLocalCluster* cluster_;
    std::unordered_map<Upstream::HostConstSharedPtr, ThreadLocalActiveClientPtr> client_map_;
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
  ConfigImpl config_;
};

} // ConnPool
} // Redis
