#pragma once

#include <functional>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/memory/utils.h"
#include "source/common/network/connection_impl.h"
#include "source/common/upstream/load_balancer_impl.h"
#include "source/extensions/filters/network/common/factory_base.h"

#include "contrib/envoy/extensions/filters/network/golang/v3alpha/golang.pb.h"
#include "contrib/golang/common/dso/dso.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Golang {

struct UpstreamConnWrapper;

class UpstreamConn : public Tcp::ConnectionPool::Callbacks,
                     public Upstream::LoadBalancerContextBase,
                     public Tcp::ConnectionPool::UpstreamCallbacks,
                     public std::enable_shared_from_this<UpstreamConn>,
                     Logger::Loggable<Logger::Id::golang> {
public:
  UpstreamConn(std::string addr, Dso::NetworkFilterDsoPtr dynamic_lib,
               Event::Dispatcher* dispatcher = nullptr);
  ~UpstreamConn() override {
    if (handler_) {
      handler_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
    }
  }

  static void initThreadLocalStorage(Server::Configuration::FactoryContext& context,
                                     ThreadLocal::SlotAllocator& tls);

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(Tcp::ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;

  void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                   Upstream::HostDescriptionConstSharedPtr host) override;

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}
  void onEvent(Network::ConnectionEvent event) override;

  // Upstream::LoadBalancerContextBase
  const Http::RequestHeaderMap* downstreamHeaders() const override { return header_map_.get(); };

  void connect();
  void write(Buffer::Instance& buf, bool end_stream);
  void close(Network::ConnectionCloseType close_type);

  Event::Dispatcher* dispatcher() { return dispatcher_; }
  void setWrapper(UpstreamConnWrapper* wrapper) { wrapper_ = wrapper; };

  std::string getLocalAddrStr() const { return addr_; }
  std::string getRemoteAddrStr() const { return remote_addr_; };

private:
  static Thread::MutexBasicLockable lock_;
  static std::vector<std::reference_wrapper<Event::Dispatcher>> dispatchers_ ABSL_GUARDED_BY(lock_);
  static int dispatcherIdx_ ABSL_GUARDED_BY(lock_);
  static ThreadLocal::SlotPtr slot_;
  // should be the singleton for use by the entire server.
  static Upstream::ClusterManager* clusterManager_;

  Dso::NetworkFilterDsoPtr dynamic_lib_{nullptr};
  UpstreamConnWrapper* wrapper_{nullptr};
  Event::Dispatcher* dispatcher_{nullptr};
  std::unique_ptr<Http::RequestHeaderMapImpl> header_map_{nullptr};
  Tcp::ConnectionPool::ConnectionDataPtr conn_{nullptr};
  Upstream::HostDescriptionConstSharedPtr host_{nullptr};
  Tcp::ConnectionPool::Cancellable* handler_{nullptr};
  bool closed_{false};
  std::string addr_{};
  std::string remote_addr_{};
};

using UpstreamConnPtr = std::shared_ptr<UpstreamConn>;

struct UpstreamConnWrapper {
public:
  UpstreamConnWrapper(UpstreamConnPtr ptr) : sharedPtr(ptr) {}
  ~UpstreamConnWrapper() = default;

  // Must be strong shared_ptr, otherwise the UpstreamConn will be released immediately since we do
  // not have any other place to keep strong reference of the UpstreamConn.
  UpstreamConnPtr sharedPtr{};
  // anchor a string temporarily, make sure it won't be freed before copied to Go.
  std::string strValue;
};

} // namespace Golang
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
