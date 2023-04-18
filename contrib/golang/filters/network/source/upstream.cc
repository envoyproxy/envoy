#include "contrib/golang/filters/network/source/upstream.h"

#include <cstdint>

#include "envoy/network/connection.h"
#include "envoy/tcp/conn_pool.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Golang {

static std::once_flag initDispatcherOnce_ = {};
std::vector<std::reference_wrapper<Event::Dispatcher>> UpstreamConn::dispatchers_ = {};
int UpstreamConn::dispatcherIdx_ = {0};
Thread::MutexBasicLockable UpstreamConn::lock_ = {};
ThreadLocal::SlotPtr UpstreamConn::slot_ = {nullptr};
Upstream::ClusterManager* UpstreamConn::clusterManager_ = {nullptr};

// init function registers each works' dispatcher for load balance when creating upstream conn
void UpstreamConn::initThreadLocalStorage(Server::Configuration::FactoryContext& context,
                                          ThreadLocal::SlotAllocator& tls) {
  // dispatchers array should be init only once.
  std::call_once(initDispatcherOnce_, [&context, &tls]() {
    // should be the singleton for use by the entire server.
    UpstreamConn::clusterManager_ = &context.clusterManager();

    UpstreamConn::slot_ = tls.allocateSlot();
    Thread::ThreadId main_thread_id = context.api().threadFactory().currentThreadId();
    UpstreamConn::slot_->set(
        [&context,
         main_thread_id](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
          if (context.api().threadFactory().currentThreadId() == main_thread_id) {
            return nullptr;
          }

          {
            Thread::LockGuard guard(UpstreamConn::lock_);
            UpstreamConn::dispatchers_.push_back(dispatcher);
          }

          return nullptr;
        });
  });
}

UpstreamConn::UpstreamConn(std::string addr, Dso::NetworkFilterDsoPtr dynamic_lib,
                           Event::Dispatcher* dispatcher)
    : dynamic_lib_(dynamic_lib), dispatcher_(dispatcher), addr_(addr) {
  if (dispatcher_ == nullptr) {
    Thread::LockGuard guard(UpstreamConn::lock_);
    // load balance among each workers' dispatcher
    dispatcher_ = &UpstreamConn::dispatchers_[UpstreamConn::dispatcherIdx_++ %
                                              UpstreamConn::dispatchers_.size()]
                       .get();
  }
  header_map_ = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
      {{Http::Headers::get().EnvoyOriginalDstHost, addr}});
}

void UpstreamConn::connect() {
  ENVOY_LOG(info, "do connect addr: {}", addr_);

  Upstream::ThreadLocalCluster* cluster =
      UpstreamConn::clusterManager_->getThreadLocalCluster("plainText");
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
  ENVOY_CONN_LOG(info, "close addr: {}, type: {}", conn_->connection(), addr_,
                 static_cast<int>(close_type));
  ASSERT(conn_ != nullptr);
  conn_->connection().close(close_type);
}

void UpstreamConn::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                               Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_CONN_LOG(info, "onPoolReady, addr: {}", conn->connection(), addr_);
  if (handler_) {
    handler_ = nullptr;
  }

  conn_ = std::move(conn);
  host_ = host;
  conn_->addUpstreamCallbacks(*this);
  remote_addr_ = conn_->connection().connectionInfoProvider().directRemoteAddress()->asString();

  dynamic_lib_->envoyGoFilterOnUpstreamConnectionReady(wrapper_);
}

void UpstreamConn::onPoolFailure(Tcp::ConnectionPool::PoolFailureReason reason,
                                 absl::string_view transport_failure_reason,
                                 Upstream::HostDescriptionConstSharedPtr) {
  ENVOY_LOG(error, "onPoolFailure, addr: {}, reason: {}, {}", addr_, int(reason),
            std::string(transport_failure_reason));
  if (handler_) {
    handler_ = nullptr;
  }

  dynamic_lib_->envoyGoFilterOnUpstreamConnectionFailure(wrapper_, static_cast<int>(reason));
}

void UpstreamConn::onEvent(Network::ConnectionEvent event) {
  ENVOY_CONN_LOG(info, "onEvent addr: {}, event: {}", conn_->connection(), addr_,
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

  Buffer::RawSliceVector sliceVector = data.getRawSlices();
  int sliceNum = sliceVector.size();
  unsigned long long* slices = new unsigned long long[2 * sliceNum];
  for (int i = 0; i < sliceNum; i++) {
    const Buffer::RawSlice& s = sliceVector[i];
    slices[2 * i] = reinterpret_cast<unsigned long long>(s.mem_);
    slices[2 * i + 1] = s.len_;
  }

  dynamic_lib_->envoyGoFilterOnUpstreamData(
      wrapper_, data.length(), reinterpret_cast<GoUint64>(slices), sliceNum, end_stream);

  // TODO: do not drain buffer by default
  data.drain(data.length());

  delete[] slices;
}

} // namespace Golang
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
