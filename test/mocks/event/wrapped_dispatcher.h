#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>

#include "envoy/event/dispatcher.h"

namespace Envoy {
namespace Event {

class WrappedDispatcher : public Dispatcher {
public:
  WrappedDispatcher(Dispatcher& impl) : impl_(impl) {}

  // Event::Dispatcher
  const std::string& name() override;
  TimeSource& timeSource() override;
  void initializeStats(Stats::Scope& scope, const absl::optional<std::string>& prefix) override;
  void clearDeferredDeleteList() override;
  Network::ConnectionPtr createServerConnection(Network::ConnectionSocketPtr&& socket,
                                                Network::TransportSocketPtr&& transport_socket,
                                                StreamInfo::StreamInfo& stream_info) override;
  Network::ClientConnectionPtr
  createClientConnection(Network::Address::InstanceConstSharedPtr address,
                         Network::Address::InstanceConstSharedPtr source_address,
                         Network::TransportSocketPtr&& transport_socket,
                         const Network::ConnectionSocket::OptionsSharedPtr& options) override;
  Network::DnsResolverSharedPtr
  createDnsResolver(const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers,
                    const bool use_tcp_for_dns_lookups) override;
  FileEventPtr createFileEvent(os_fd_t fd, FileReadyCb cb, FileTriggerType trigger,
                               uint32_t events) override;
  Filesystem::WatcherPtr createFilesystemWatcher() override;
  Network::ListenerPtr createListener(Network::SocketSharedPtr&& socket,
                                      Network::TcpListenerCallbacks& cb, bool bind_to_port,
                                      uint32_t backlog_size) override;
  Network::UdpListenerPtr createUdpListener(Network::SocketSharedPtr&& socket,
                                            Network::UdpListenerCallbacks& cb) override;
  TimerPtr createTimer(TimerCb cb) override;
  Event::SchedulableCallbackPtr createSchedulableCallback(std::function<void()> cb) override;
  void deferredDelete(DeferredDeletablePtr&& to_delete) override;
  void exit() override;
  SignalEventPtr listenForSignal(int signal_num, SignalCb cb) override;
  void post(std::function<void()> callback) override;
  void run(RunType type) override;
  Buffer::WatermarkFactory& getWatermarkFactory() override;
  const ScopeTrackedObject* setTrackedObject(const ScopeTrackedObject* object) override;
  bool isThreadSafe() const override;
  MonotonicTime approximateMonotonicTime() const override;
  void updateApproximateMonotonicTime() override;

protected:
  Dispatcher& impl_;
};

} // namespace Event
} // namespace Envoy