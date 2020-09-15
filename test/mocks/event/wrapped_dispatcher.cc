#include "test/mocks/event/wrapped_dispatcher.h"

namespace Envoy {
namespace Event {

const std::string& WrappedDispatcher::name() { return impl_.name(); }

TimeSource& WrappedDispatcher::timeSource() { return impl_.timeSource(); }

void WrappedDispatcher::initializeStats(Stats::Scope& scope,
                                        const absl::optional<std::string>& prefix) {
  impl_.initializeStats(scope, prefix);
}

void WrappedDispatcher::clearDeferredDeleteList() { impl_.clearDeferredDeleteList(); }

Network::ConnectionPtr
WrappedDispatcher::createServerConnection(Network::ConnectionSocketPtr&& socket,
                                          Network::TransportSocketPtr&& transport_socket,
                                          StreamInfo::StreamInfo& stream_info) {
  return impl_.createServerConnection(std::move(socket), std::move(transport_socket), stream_info);
}

Network::ClientConnectionPtr WrappedDispatcher::createClientConnection(
    Network::Address::InstanceConstSharedPtr address,
    Network::Address::InstanceConstSharedPtr source_address,
    Network::TransportSocketPtr&& transport_socket,
    const Network::ConnectionSocket::OptionsSharedPtr& options) {
  return impl_.createClientConnection(std::move(address), std::move(source_address),
                                      std::move(transport_socket), options);
}

Network::DnsResolverSharedPtr WrappedDispatcher::createDnsResolver(
    const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers,
    const bool use_tcp_for_dns_lookups) {
  return impl_.createDnsResolver(resolvers, use_tcp_for_dns_lookups);
}

FileEventPtr WrappedDispatcher::createFileEvent(os_fd_t fd, FileReadyCb cb, FileTriggerType trigger,
                                                uint32_t events) {
  return impl_.createFileEvent(fd, cb, trigger, events);
}

Filesystem::WatcherPtr WrappedDispatcher::createFilesystemWatcher() {
  return impl_.createFilesystemWatcher();
}

Network::ListenerPtr WrappedDispatcher::createListener(Network::SocketSharedPtr&& socket,
                                                       Network::TcpListenerCallbacks& cb,
                                                       bool bind_to_port, uint32_t backlog_size) {
  return impl_.createListener(std::move(socket), cb, bind_to_port, backlog_size);
}

Network::UdpListenerPtr WrappedDispatcher::createUdpListener(Network::SocketSharedPtr&& socket,
                                                             Network::UdpListenerCallbacks& cb) {
  return impl_.createUdpListener(std::move(socket), cb);
}

TimerPtr WrappedDispatcher::createTimer(TimerCb cb) { return impl_.createTimer(std::move(cb)); }

Event::SchedulableCallbackPtr
WrappedDispatcher::createSchedulableCallback(std::function<void()> cb) {
  return impl_.createSchedulableCallback(std::move(cb));
}

void WrappedDispatcher::deferredDelete(DeferredDeletablePtr&& to_delete) {
  impl_.deferredDelete(std::move(to_delete));
}

void WrappedDispatcher::exit() { impl_.exit(); }

SignalEventPtr WrappedDispatcher::listenForSignal(int signal_num, SignalCb cb) {
  return impl_.listenForSignal(signal_num, std::move(cb));
}

void WrappedDispatcher::post(std::function<void()> callback) { impl_.post(std::move(callback)); }

void WrappedDispatcher::run(RunType type) { impl_.run(type); }

Buffer::WatermarkFactory& WrappedDispatcher::getWatermarkFactory() {
  return impl_.getWatermarkFactory();
}
const ScopeTrackedObject* WrappedDispatcher::setTrackedObject(const ScopeTrackedObject* object) {
  return impl_.setTrackedObject(object);
}

MonotonicTime WrappedDispatcher::approximateMonotonicTime() const {
  return impl_.approximateMonotonicTime();
}

void WrappedDispatcher::updateApproximateMonotonicTime() { impl_.updateApproximateMonotonicTime(); }

bool WrappedDispatcher::isThreadSafe() const { return impl_.isThreadSafe(); }

} // namespace Event
} // namespace Envoy