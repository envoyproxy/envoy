#pragma once

#include <list>

#include "envoy/http/async_client.h"
#include "envoy/singleton/instance.h"
#include "envoy/singleton/manager.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/upstream/od_cds_context.h"

namespace Envoy {
namespace Http {

// Worker-local adapter used before a per-cluster AsyncClient exists. Requests retain their stable
// handle through discovery and streaming; the ordinary async client owns HTTP routing and retries.
class OnDemandAsyncClient : public ThreadLocal::ThreadLocalObject {
public:
  enum class DiscoveryResult { Available, Missing, Timeout, Cancelled, BufferOverflow };
  using DiscoveryCallback = std::function<void(DiscoveryResult, std::chrono::milliseconds)>;
  OnDemandAsyncClient(Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher);
  ~OnDemandAsyncClient() override;
  Event::Dispatcher& dispatcher() { return dispatcher_; }

  // discovery_context must be non-null and is retained by the request. Pending request bodies
  // exceeding options.buffer_limit_ are discarded without applying downstream backpressure.
  AsyncClient::OngoingRequest*
  startRequest(const std::string& cluster, RequestHeaderMapPtr&& headers,
               AsyncClient::Callbacks& callbacks, const AsyncClient::RequestOptions& options,
               const Upstream::OdCdsContextSharedPtr& discovery_context, bool end_stream,
               DiscoveryCallback discovery_callback);

private:
  class Request;
  Upstream::ClusterManager& cm_;
  Event::Dispatcher& dispatcher_;
  std::list<std::unique_ptr<Request>> requests_;
};

// Shared by configurations that enable mirror discovery. The slot is allocated on the main
// thread; each worker creates its client only when a request actually needs discovery.
class OnDemandAsyncClientProvider : public Singleton::Instance {
public:
  static std::shared_ptr<OnDemandAsyncClientProvider> getOrCreate(Upstream::ClusterManager& cm,
                                                                  ThreadLocal::Instance& tls,
                                                                  Singleton::Manager& manager);
  OnDemandAsyncClient& get();

private:
  OnDemandAsyncClientProvider(Upstream::ClusterManager& cm, ThreadLocal::Instance& tls);

  struct ThreadLocalClient : public ThreadLocal::ThreadLocalObject {
    std::unique_ptr<OnDemandAsyncClient> client_;
  };

  Upstream::ClusterManager& cm_;
  ThreadLocal::Instance& tls_;
  ThreadLocal::TypedSlot<ThreadLocalClient> clients_;
};

} // namespace Http
} // namespace Envoy
