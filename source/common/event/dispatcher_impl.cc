#include "dispatcher_impl.h"
#include "file_event_impl.h"
#include "signal_impl.h"
#include "timer_impl.h"

#include "envoy/network/listener.h"
#include "envoy/network/listen_socket.h"

#include "common/filesystem/watcher_impl.h"
#include "common/network/connection_impl.h"
#include "common/network/dns_impl.h"
#include "common/network/listener_impl.h"
#include "common/ssl/connection_impl.h"

#include "event2/bufferevent_ssl.h"
#include "event2/event.h"

namespace Event {

DispatcherImpl::DispatcherImpl()
    : base_(event_base_new()),
      deferred_delete_timer_(createTimer([this]() -> void { clearDeferredDeleteList(); })) {}

DispatcherImpl::~DispatcherImpl() {}

void DispatcherImpl::clearDeferredDeleteList() {
  while (!to_delete_.empty()) {
    // The destructor of a deferred deletion item can yield more deferred deletion. Loop
    // and destroy all of them until there is nothing left.
    std::list<DeferredDeletablePtr> copy(std::move(to_delete_));
    log_trace("clearing deferred deletion list (size={})", copy.size());
    copy.clear();
  }
}

Network::ClientConnectionPtr DispatcherImpl::createClientConnection(const std::string& url) {
  Event::Libevent::BufferEventPtr bev{
      bufferevent_socket_new(base_.get(), -1, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS)};
  return Network::ClientConnectionImpl::create(*this, std::move(bev), url);
}

Network::ClientConnectionPtr DispatcherImpl::createSslClientConnection(Ssl::ClientContext& ssl_ctx,
                                                                       const std::string& url) {
  // The dynamic_cast is necessary here in order to avoid exposing the SSL_CTX directly from
  // Ssl::Context.
  Ssl::ContextImpl& ctx = dynamic_cast<Ssl::ContextImpl&>(ssl_ctx);
  Event::Libevent::BufferEventPtr bev{
      bufferevent_openssl_socket_new(base_.get(), -1, ctx.newSsl(), BUFFEREVENT_SSL_CONNECTING,
                                     BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS)};

  return Network::ClientConnectionPtr{
      new Ssl::ClientConnectionImpl(*this, std::move(bev), ctx, url)};
}

Network::DnsResolverPtr DispatcherImpl::createDnsResolver() {
  return Network::DnsResolverPtr{new Network::DnsResolverImpl(*this)};
}

FileEventPtr DispatcherImpl::createFileEvent(int fd, FileReadyCb read_cb, FileReadyCb write_cb) {
  return FileEventPtr{new FileEventImpl(*this, fd, read_cb, write_cb)};
}

Filesystem::WatcherPtr DispatcherImpl::createFilesystemWatcher() {
  return Filesystem::WatcherPtr{new Filesystem::WatcherImpl(*this)};
}

Network::ListenerPtr DispatcherImpl::createListener(Network::ListenSocket& socket,
                                                    Network::ListenerCallbacks& cb,
                                                    Stats::Store& stats_store,
                                                    bool use_proxy_proto) {
  return Network::ListenerPtr{
      new Network::ListenerImpl(*this, socket, cb, stats_store, use_proxy_proto)};
}

Network::ListenerPtr DispatcherImpl::createSslListener(Ssl::ServerContext& ssl_ctx,
                                                       Network::ListenSocket& socket,
                                                       Network::ListenerCallbacks& cb,
                                                       Stats::Store& stats_store,
                                                       bool use_proxy_proto) {
  return Network::ListenerPtr{
      new Network::SslListenerImpl(*this, ssl_ctx, socket, cb, stats_store, use_proxy_proto)};
}

TimerPtr DispatcherImpl::createTimer(TimerCb cb) { return TimerPtr{new TimerImpl(*this, cb)}; }

void DispatcherImpl::deferredDelete(DeferredDeletablePtr&& to_delete) {
  to_delete_.emplace_back(std::move(to_delete));
  log_trace("item added to deferred deletion list (size={})", to_delete_.size());
  if (1 == to_delete_.size()) {
    deferred_delete_timer_->enableTimer(std::chrono::milliseconds(0));
  }
}

void DispatcherImpl::exit() { event_base_loopexit(base_.get(), nullptr); }

SignalEventPtr DispatcherImpl::listenForSignal(int signal_num, SignalCb cb) {
  return SignalEventPtr{new SignalEventImpl(*this, signal_num, cb)};
}

void DispatcherImpl::post(std::function<void()> callback) {
  bool do_post;
  {
    std::unique_lock<std::mutex> lock(post_lock_);
    do_post = post_callbacks_.empty();
    post_callbacks_.push_back(callback);
  }

  if (do_post) {
    // If the dispatcher shuts down before this runs, we will leak. This never happens during
    // normal operation so its not a big deal.
    event_base_once(base_.get(), -1, EV_TIMEOUT, [](evutil_socket_t, short, void* arg) -> void {
      static_cast<DispatcherImpl*>(arg)->runPostCallbacks();
    }, this, nullptr);
  }
}

void DispatcherImpl::run(RunType type) {
  // Flush all post callbacks before we run the event loop. We do this because there are post
  // callbacks that have to get run before the initial event loop starts running. libevent does
  // not gaurantee that events are run in any particular order. So even if we post() and call
  // event_base_once() before some other event, the other event might get called first.
  runPostCallbacks();

  event_base_loop(base_.get(), type == RunType::NonBlock ? EVLOOP_NONBLOCK : 0);
}

void DispatcherImpl::runPostCallbacks() {
  std::unique_lock<std::mutex> lock(post_lock_);
  while (!post_callbacks_.empty()) {
    std::function<void()> callback = post_callbacks_.front();
    post_callbacks_.pop_front();

    lock.unlock();
    callback();
    lock.lock();
  }
}

} // Event
