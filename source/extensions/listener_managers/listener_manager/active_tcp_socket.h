#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>

#include "envoy/common/time.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"

#include "source/common/common/linked_object.h"
#include "source/common/network/listener_filter_buffer_impl.h"
#include "source/common/network/listener_filter_manager_impl_base.h"
#include "source/server/active_listener_base.h"

namespace Envoy {
namespace Server {

class ActiveStreamListenerBase;

/**
 * Wrapper for an active accepted socket owned by the active tcp listener.
 */
class ActiveTcpSocket : public Network::ListenerFilterManagerImplBase,
                        public Network::ListenerFilterCallbacks,
                        public LinkedObject<ActiveTcpSocket>,
                        public Event::DeferredDeletable,
                        Logger::Loggable<Logger::Id::conn_handler> {
public:
  ActiveTcpSocket(ActiveStreamListenerBase& listener, Network::ConnectionSocketPtr&& socket,
                  bool hand_off_restored_destination_connections);
  ~ActiveTcpSocket() override;

  void onTimeout();
  void startTimer();
  void unlink();
  void newConnection();

  // Network::ListenerFilterManagerImplBase
  void startFilterChain() override { continueFilterChain(true); }

  // Network::ListenerFilterCallbacks
  Network::ConnectionSocket& socket() override { return *socket_.get(); }
  Event::Dispatcher& dispatcher() override;
  void continueFilterChain(bool success) override;
  void setDynamicMetadata(const std::string& name, const ProtobufWkt::Struct& value) override;
  envoy::config::core::v3::Metadata& dynamicMetadata() override {
    return stream_info_->dynamicMetadata();
  };
  const envoy::config::core::v3::Metadata& dynamicMetadata() const override {
    return stream_info_->dynamicMetadata();
  };
  StreamInfo::FilterState& filterState() override { return *stream_info_->filterState().get(); }
  StreamInfo::StreamInfo* streamInfo() const { return stream_info_.get(); }
  bool connected() const { return connected_; }
  bool isEndFilterIteration() const { return iter_ == accept_filters_.end(); }

private:
  void createListenerFilterBuffer();

  // The owner of this ActiveTcpSocket.
  ActiveStreamListenerBase& listener_;
  Network::ConnectionSocketPtr socket_;
  const bool hand_off_restored_destination_connections_;
  std::list<Network::ListenerFilterPtr>::iterator iter_;
  Event::TimerPtr timer_;
  std::unique_ptr<StreamInfo::StreamInfo> stream_info_;
  bool connected_{false};

  Network::ListenerFilterBufferImplPtr listener_filter_buffer_;
};

} // namespace Server
} // namespace Envoy
