#include "contrib/golang/filters/network/source/upstream.h"

#include <cstdint>

#include "envoy/network/connection.h"
#include "envoy/tcp/conn_pool.h"

#include "source/common/common/assert.h"
#include "source/common/network/filter_state_dst_address.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Golang {

// init function registers each works' dispatcher for load balance when creating upstream conn
void UpstreamConn::initThreadLocalStorage(Server::Configuration::FactoryContext& context,
                                          ThreadLocal::SlotAllocator& tls) {
  // dispatchers array should be init only once.
  DispatcherStore& store = dispatcherStore();
  std::call_once(store.init_once_, [&context, &tls, &store]() {
    // should be the singleton for use by the entire server.
    ClusterManagerContainer& cluster_manager_container = clusterManagerContainer();
    cluster_manager_container.cluster_manager_ = &context.serverFactoryContext().clusterManager();

    SlotPtrContainer& slot_ptr_container = slotPtrContainer();
    slot_ptr_container.slot_ = tls.allocateSlot();

    Thread::ThreadId main_thread_id =
        context.serverFactoryContext().api().threadFactory().currentThreadId();
    slot_ptr_container.slot_->set(
        [&context, main_thread_id,
         &store](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
          if (context.serverFactoryContext().api().threadFactory().currentThreadId() ==
              main_thread_id) {
            return nullptr;
          }

          {
            Thread::LockGuard guard(store.lock_);
            store.dispatchers_.push_back(dispatcher);
          }

          return nullptr;
        });
  });
}

UpstreamConn::UpstreamConn(std::string addr, Dso::NetworkFilterDsoPtr dynamic_lib,
                           unsigned long long int go_conn_id, Event::Dispatcher* dispatcher)
    : dynamic_lib_(dynamic_lib), go_conn_id_(go_conn_id), dispatcher_(dispatcher), addr_(addr) {
  if (dispatcher_ == nullptr) {
    DispatcherStore& store = dispatcherStore();
    Thread::LockGuard guard(store.lock_);
    // load balance among each workers' dispatcher
    ASSERT(!store.dispatchers_.empty());
    dispatcher_ = &store.dispatchers_[store.dispatcher_idx_++ % store.dispatchers_.size()].get();
  }
  stream_info_ = std::make_unique<StreamInfo::StreamInfoImpl>(
      dispatcher_->timeSource(), nullptr, StreamInfo::FilterState::LifeSpan::FilterChain);
  auto address = std::make_shared<Network::AddressObject>(
      Network::Utility::parseInternetAddressAndPortNoThrow(addr, false));
  if (!address) {
    throwEnvoyExceptionOrPanic(absl::StrCat("malformed IP address: ", addr));
  }
  stream_info_->filterState()->setData("envoy.network.transport_socket.original_dst_address",
                                       address, StreamInfo::FilterState::StateType::ReadOnly,
                                       StreamInfo::FilterState::LifeSpan::FilterChain,
                                       StreamInfo::StreamSharingMayImpactPooling::None);
}

void UpstreamConn::connect() {
  ENVOY_LOG(debug, "do connect addr: {}", addr_);

  // TODO(antJack): add support for upstream TLS cluster.
  static const std::string upstream_cluster = "plainText";
  ClusterManagerContainer& cluster_manager_container = clusterManagerContainer();
  Upstream::ThreadLocalCluster* cluster =
      cluster_manager_container.cluster_manager_->getThreadLocalCluster(upstream_cluster);
  if (!cluster) {
    ENVOY_LOG(error, "cluster not found");
    onPoolFailure(Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                  absl::string_view("cluster not found"), nullptr);
    return;
  }

  auto conn_pool = cluster->tcpConnPool(Upstream::ResourcePriority::Default, this);
  if (!conn_pool) {
    ENVOY_LOG(error, "no host available for cluster");
    // prevent golang from blocking on connection result channel
    onPoolFailure(Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                  absl::string_view("no host available"), nullptr);
    return;
  }

  Tcp::ConnectionPool::Cancellable* cancellable = conn_pool.value().newConnection(*this);
  if (cancellable) {
    handler_ = cancellable;
  }
}

void UpstreamConn::enableHalfClose(bool enabled) {
  if (closed_) {
    ENVOY_LOG(warn, "connection has closed, addr: {}", addr_);
    return;
  }
  ASSERT(conn_ != nullptr);
  conn_->connection().enableHalfClose(enabled);
  ENVOY_CONN_LOG(debug, "set enableHalfClose to addr: {}, enabled: {}, actualEnabled: {}",
                 conn_->connection(), addr_, enabled, conn_->connection().isHalfCloseEnabled());
}

void UpstreamConn::write(Buffer::Instance& buf, bool end_stream) {
  if (closed_) {
    ENVOY_LOG(warn, "connection has closed, addr: {}", addr_);
    return;
  }
  ENVOY_CONN_LOG(debug, "write to addr: {}, len: {}, end: {}", conn_->connection(), addr_,
                 buf.length(), end_stream);
  ASSERT(conn_ != nullptr);
  conn_->connection().write(buf, end_stream);
}

void UpstreamConn::close(Network::ConnectionCloseType close_type) {
  if (closed_) {
    ENVOY_LOG(warn, "connection has closed, addr: {}", addr_);
    return;
  }
  ENVOY_CONN_LOG(debug, "close addr: {}, type: {}", conn_->connection(), addr_,
                 static_cast<int>(close_type));
  ASSERT(conn_ != nullptr);
  conn_->connection().close(close_type, "go_upstream_close");
}

void UpstreamConn::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                               Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_CONN_LOG(debug, "onPoolReady, addr: {}", conn->connection(), addr_);
  if (handler_) {
    handler_ = nullptr;
  }

  conn_ = std::move(conn);
  host_ = host;
  conn_->addUpstreamCallbacks(*this);
  remote_addr_ = conn_->connection().connectionInfoProvider().directRemoteAddress()->asString();

  dynamic_lib_->envoyGoFilterOnUpstreamConnectionReady(wrapper_, go_conn_id_);
}

void UpstreamConn::onPoolFailure(Tcp::ConnectionPool::PoolFailureReason reason,
                                 absl::string_view transport_failure_reason,
                                 Upstream::HostDescriptionConstSharedPtr) {
  ENVOY_LOG(error, "onPoolFailure, addr: {}, reason: {}, {}", addr_, int(reason),
            std::string(transport_failure_reason));
  if (handler_) {
    handler_ = nullptr;
  }

  dynamic_lib_->envoyGoFilterOnUpstreamConnectionFailure(wrapper_, static_cast<int>(reason),
                                                         go_conn_id_);
}

void UpstreamConn::onEvent(Network::ConnectionEvent event) {
  ENVOY_CONN_LOG(debug, "onEvent addr: {}, event: {}", conn_->connection(), addr_,
                 static_cast<int>(event));
  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    closed_ = true;
    conn_ = nullptr;
  }

  dynamic_lib_->envoyGoFilterOnUpstreamEvent(wrapper_, static_cast<int>(event));
}

void UpstreamConn::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(debug, "onData, addr: {}, len: {}, end: {}", conn_->connection(), addr_,
                 data.length(), end_stream);

  Buffer::RawSliceVector slice_vector = data.getRawSlices();
  int slice_num = slice_vector.size();
  unsigned long long* slices = new unsigned long long[2 * slice_num];
  for (int i = 0; i < slice_num; i++) {
    const Buffer::RawSlice& s = slice_vector[i];
    slices[2 * i] = reinterpret_cast<unsigned long long>(s.mem_);
    slices[2 * i + 1] = s.len_;
  }

  dynamic_lib_->envoyGoFilterOnUpstreamData(
      wrapper_, data.length(), reinterpret_cast<GoUint64>(slices), slice_num, end_stream);

  // TODO: do not drain buffer by default
  data.drain(data.length());

  delete[] slices;
}

} // namespace Golang
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
