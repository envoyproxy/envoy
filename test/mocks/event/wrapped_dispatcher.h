#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>

#include "envoy/event/dispatcher.h"

namespace Envoy {
namespace Event {

// Dispatcher implementation that forwards all methods to another implementation
// class. Subclassing this provides a convenient way to forward most methods and
// override the behavior of a few.
class WrappedDispatcher : public Dispatcher {
public:
  WrappedDispatcher(Dispatcher& impl) : impl_(impl) {}

  // Event::Dispatcher
  const std::string& name() override { return impl_.name(); }

  TimeSource& timeSource() override { return impl_.timeSource(); }

  void initializeStats(Stats::Scope& scope, const absl::optional<std::string>& prefix) override {
    impl_.initializeStats(scope, prefix);
  }

  void clearDeferredDeleteList() override { impl_.clearDeferredDeleteList(); }

  Network::ServerConnectionPtr
  createServerConnection(Network::ConnectionSocketPtr&& socket,
                         Network::TransportSocketPtr&& transport_socket,
                         StreamInfo::StreamInfo& stream_info) override {
    return impl_.createServerConnection(std::move(socket), std::move(transport_socket),
                                        stream_info);
  }

  Network::ClientConnectionPtr
  createClientConnection(Network::Address::InstanceConstSharedPtr address,
                         Network::Address::InstanceConstSharedPtr source_address,
                         Network::TransportSocketPtr&& transport_socket,
                         const Network::ConnectionSocket::OptionsSharedPtr& options) override {
    return impl_.createClientConnection(std::move(address), std::move(source_address),
                                        std::move(transport_socket), options);
  }

  Network::DnsResolverSharedPtr
  createDnsResolver(const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers,
                    const bool use_tcp_for_dns_lookups) override {
    return impl_.createDnsResolver(resolvers, use_tcp_for_dns_lookups);
  }

  FileEventPtr createFileEvent(os_fd_t fd, FileReadyCb cb, FileTriggerType trigger,
                               uint32_t events) override {
    return impl_.createFileEvent(fd, cb, trigger, events);
  }

  Filesystem::WatcherPtr createFilesystemWatcher() override {
    return impl_.createFilesystemWatcher();
  }

  Network::ListenerPtr createListener(Network::SocketSharedPtr&& socket,
                                      Network::TcpListenerCallbacks& cb, bool bind_to_port,
                                      uint32_t backlog_size) override {
    return impl_.createListener(std::move(socket), cb, bind_to_port, backlog_size);
  }

  Network::UdpListenerPtr createUdpListener(Network::SocketSharedPtr socket,
                                            Network::UdpListenerCallbacks& cb) override {
    return impl_.createUdpListener(std::move(socket), cb);
  }

  TimerPtr createTimer(TimerCb cb) override { return impl_.createTimer(std::move(cb)); }

  Event::SchedulableCallbackPtr createSchedulableCallback(std::function<void()> cb) override {
    return impl_.createSchedulableCallback(std::move(cb));
  }

  void deferredDelete(DeferredDeletablePtr&& to_delete) override {
    impl_.deferredDelete(std::move(to_delete));
  }

  void exit() override { impl_.exit(); }

  SignalEventPtr listenForSignal(int signal_num, SignalCb cb) override {
    return impl_.listenForSignal(signal_num, std::move(cb));
  }

  void post(std::function<void()> callback) override { impl_.post(std::move(callback)); }

  void run(RunType type) override { impl_.run(type); }

  Buffer::WatermarkFactory& getWatermarkFactory() override { return impl_.getWatermarkFactory(); }
  const ScopeTrackedObject* setTrackedObject(const ScopeTrackedObject* object) override {
    return impl_.setTrackedObject(object);
  }

  MonotonicTime approximateMonotonicTime() const override {
    return impl_.approximateMonotonicTime();
  }

  void updateApproximateMonotonicTime() override { impl_.updateApproximateMonotonicTime(); }

  bool isThreadSafe() const override { return impl_.isThreadSafe(); }

protected:
  Dispatcher& impl_;
};

} // namespace Event
} // namespace Envoy