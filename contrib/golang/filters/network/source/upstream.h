#pragma once

#include <functional>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/memory/utils.h"
#include "source/common/network/connection_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
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
               unsigned long long int go_conn_id, Event::Dispatcher* dispatcher = nullptr);
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
  const StreamInfo::StreamInfo* requestStreamInfo() const override { return stream_info_.get(); }

  void connect();
  void write(Buffer::Instance& buf, bool end_stream);
  void close(Network::ConnectionCloseType close_type);

  Event::Dispatcher* dispatcher() { return dispatcher_; }
  void setWrapper(UpstreamConnWrapper* wrapper) { wrapper_ = wrapper; };

  std::string getLocalAddrStr() const { return addr_; }
  std::string getRemoteAddrStr() const { return remote_addr_; };

private:
  struct DispatcherStore {
    std::vector<std::reference_wrapper<Event::Dispatcher>> dispatchers_ ABSL_GUARDED_BY(lock_){};
    int dispatcher_idx_ ABSL_GUARDED_BY(lock_){0};
    Thread::MutexBasicLockable lock_{};
    std::once_flag init_once_{};
  };
  static DispatcherStore& dispatcherStore() { MUTABLE_CONSTRUCT_ON_FIRST_USE(DispatcherStore); }

  struct SlotPtrContainer {
    ThreadLocal::SlotPtr slot_{nullptr};
  };
  static SlotPtrContainer& slotPtrContainer() { MUTABLE_CONSTRUCT_ON_FIRST_USE(SlotPtrContainer); }

  struct ClusterManagerContainer {
    Upstream::ClusterManager* cluster_manager_{nullptr};
  };
  static ClusterManagerContainer& clusterManagerContainer() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(ClusterManagerContainer);
  }

  Dso::NetworkFilterDsoPtr dynamic_lib_{nullptr};
  unsigned long long int go_conn_id_{0};
  UpstreamConnWrapper* wrapper_{nullptr};
  Event::Dispatcher* dispatcher_{nullptr};
  std::unique_ptr<StreamInfo::StreamInfo> stream_info_{nullptr};
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
  UpstreamConnWrapper(UpstreamConnPtr ptr) : conn_ptr_(ptr) {}
  ~UpstreamConnWrapper() = default;

  // Must be strong shared_ptr, otherwise the UpstreamConn will be released immediately since we do
  // not have any other place to keep strong reference of the UpstreamConn.
  UpstreamConnPtr conn_ptr_{};
  // anchor a string temporarily, make sure it won't be freed before copied to Go.
  std::string str_value_;
};

} // namespace Golang
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
