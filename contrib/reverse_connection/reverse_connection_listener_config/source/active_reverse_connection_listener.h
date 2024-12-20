#pragma once
#include <atomic>
#include <cstdint>
#include <list>
#include <memory>

#include "contrib/reverse_connection/bootstrap/source/reverse_conn_thread_local_registry.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_manager.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_handler.h"

#include "envoy/event/dispatcher.h"
#include "envoy/network/filter.h"
#include "envoy/network/listener.h"

#include "source/common/listener_manager/active_stream_listener_base.h"
#include "source/server/active_listener_base.h"


namespace Envoy {
namespace Extensions {
namespace ReverseConnection {
class ActiveReverseConnectionListener : public Server::OwnedActiveStreamListenerBase,
                                        public Network::TcpListenerCallbacks,
                                        public Network::ReverseConnectionListener {
public:
  ActiveReverseConnectionListener(Network::ConnectionHandler& conn_handler,
                                  Event::Dispatcher& dispatcher, Network::ListenerConfig& config, Bootstrap::ReverseConnection::RCThreadLocalRegistry& local_registry);
  ActiveReverseConnectionListener(Network::ConnectionHandler& conn_handler,
                                  Event::Dispatcher& dispatcher, Network::ListenerPtr listener,
                                  Network::ListenerConfig& config, Bootstrap::ReverseConnection::RCThreadLocalRegistry& local_registry);
  ~ActiveReverseConnectionListener() override;

  class NetworkReverseConnectionListener : public Network::Listener {

  public:
    // ReverseConnectionListener does not bind to port.
    void disable() override {
      ENVOY_LOG(debug, "Warning: the reverse connection listener cannot be disabled.");
    }

    void enable() override {
      ENVOY_LOG(debug, "Warning: the reverse connection listener is always enabled.");
    }

    void setRejectFraction(UnitFloat) override {}
    void configureLoadShedPoints(Server::LoadShedPointProvider&) override {}
    bool shouldBypassOverloadManager() const override { return false; }
  };

  virtual void removeConnection(Server::ActiveTcpConnection& connection) override;

  Network::BalancedConnectionHandlerOptRef
  getBalancedHandlerByAddress(const Network::Address::Instance&) override {
    // Reverse connection listener doesn't support migrate connection to another worker.
    PANIC("not implemented");
  }

  void onAccept(Network::ConnectionSocketPtr&& socket) override;

  // Network::TcpListenerCallbacks
  void onReject(RejectCause) override;
  void recordConnectionsAcceptedOnSocketEvent(uint32_t connections_accepted) override;

  // ConnectionHandler::ActiveListener
  uint64_t listenerTag() override { return OwnedActiveStreamListenerBase::listenerTag(); }
  Network::Listener* listener() override { return listener_.get(); }
  void pauseListening() override {
    if (listener_ != nullptr) {
      listener_->disable();
    }
  }
  void resumeListening() override {
    if (listener_ != nullptr) {
      listener_->enable();
    }
  }
  void shutdownListener(const Network::ExtraShutdownListenerOptions&) override {
    listener_.reset();
  }
  void updateListenerConfig(Network::ListenerConfig& config) override;
  void onFilterChainDraining(
      const std::list<const Network::FilterChain*>& draining_filter_chains) override {
    OwnedActiveStreamListenerBase::onFilterChainDraining(draining_filter_chains);
  }

  void startRCWorkflow(Event::Dispatcher& dispatcher, Network::ListenerConfig& config) override;

  // ActiveStreamListenerBase
  void incNumConnections() override { config_->openConnections().inc(); }
  void decNumConnections() override { config_->openConnections().dec(); }

  void newActiveConnection(const Network::FilterChain&, Network::ServerConnectionPtr,
                           std::unique_ptr<StreamInfo::StreamInfo>) override;

  private:
    Bootstrap::ReverseConnection::RCThreadLocalRegistry& local_registry_;
};
} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy
