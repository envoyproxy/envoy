#pragma once

#include "envoy/network/listener.h"

#include "source/common/network/generic_listener_filter_impl_base.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_network_connection.h"

#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_packets.h"

namespace Envoy {
namespace Quic {

// A subclass to implement the extra QUIC listener filter interfaces.
class QuicListenerFilterWrapper
    : public Network::GenericListenerFilterImplBase<Network::QuicListenerFilter> {
public:
  QuicListenerFilterWrapper(const Network::ListenerFilterMatcherSharedPtr& matcher,
                            Network::QuicListenerFilterPtr listener_filter)
      : Network::GenericListenerFilterImplBase<Network::QuicListenerFilter>(
            std::move(matcher), std::move(listener_filter)) {}

  // Network::QuicListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override {
    if (isDisabled(cb)) {
      on_accept_by_passed_ = true;
    }
    return Network::GenericListenerFilterImplBase<Network::QuicListenerFilter>::onAccept(cb);
  }
  bool isCompatibleWithServerPreferredAddress(
      const quic::QuicSocketAddress& server_preferred_address) const override {
    if (on_accept_by_passed_) {
      return true;
    }
    return listener_filter_->isCompatibleWithServerPreferredAddress(server_preferred_address);
  }
  Network::FilterStatus onPeerAddressChanged(const quic::QuicSocketAddress& new_address,
                                             Network::Connection& connection) override {
    if (on_accept_by_passed_) {
      return Network::FilterStatus::Continue;
    }
    return listener_filter_->onPeerAddressChanged(new_address, connection);
  }
  Network::FilterStatus onFirstPacketReceived(const quic::QuicReceivedPacket& packet) override {
    if (on_accept_by_passed_) {
      return Network::FilterStatus::Continue;
    }
    return listener_filter_->onFirstPacketReceived(packet);
  }

private:
  bool on_accept_by_passed_{false};
};

// An object created on the stack during QUIC connection creation to apply listener filters, if
// there is any, within the call stack.
class QuicListenerFilterManagerImpl : public Network::QuicListenerFilterManager,
                                      public Network::ListenerFilterCallbacks {
public:
  QuicListenerFilterManagerImpl(Event::Dispatcher& dispatcher, Network::ConnectionSocket& socket,
                                StreamInfo::StreamInfo& stream_info)
      : socket_(socket), stream_info_(stream_info), dispatcher_(dispatcher) {}

  // Network::ListenerFilterCallbacks
  Network::ConnectionSocket& socket() override { return socket_; }
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  void continueFilterChain(bool /*success*/) override { IS_ENVOY_BUG("Should not be used."); }
  void useOriginalDst(bool /*use_original_dst*/) override { IS_ENVOY_BUG("Should not be used."); }
  void setDynamicMetadata(const std::string& name, const ProtobufWkt::Struct& value) override {
    stream_info_.setDynamicMetadata(name, value);
  }
  void setDynamicTypedMetadata(const std::string& name, const ProtobufWkt::Any& value) override {
    stream_info_.setDynamicTypedMetadata(name, value);
  }
  envoy::config::core::v3::Metadata& dynamicMetadata() override {
    return stream_info_.dynamicMetadata();
  };
  const envoy::config::core::v3::Metadata& dynamicMetadata() const override {
    return stream_info_.dynamicMetadata();
  };
  StreamInfo::FilterState& filterState() override { return *stream_info_.filterState().get(); }

  // Network::QuicListenerFilterManager
  void addFilter(const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
                 Network::QuicListenerFilterPtr&& filter) override {
    accept_filters_.emplace_back(
        std::make_unique<QuicListenerFilterWrapper>(listener_filter_matcher, std::move(filter)));
  }
  bool shouldAdvertiseServerPreferredAddress(
      const quic::QuicSocketAddress& server_preferred_address) const override {
    for (const Network::QuicListenerFilterPtr& accept_filter : accept_filters_) {
      if (!accept_filter->isCompatibleWithServerPreferredAddress(server_preferred_address)) {
        return false;
      }
    }
    return true;
  }
  void onPeerAddressChanged(const quic::QuicSocketAddress& new_address,
                            Network::Connection& connection) override {
    for (Network::QuicListenerFilterPtr& accept_filter : accept_filters_) {
      Network::FilterStatus status = accept_filter->onPeerAddressChanged(new_address, connection);
      if (status == Network::FilterStatus::StopIteration ||
          connection.state() != Network::Connection::State::Open) {
        return;
      }
    }
  }
  void startFilterChain() {
    for (Network::QuicListenerFilterPtr& accept_filter : accept_filters_) {
      Network::FilterStatus status = accept_filter->onAccept(*this);
      if (status == Network::FilterStatus::StopIteration || !socket().ioHandle().isOpen()) {
        break;
      }
    }
  }
  void onFirstPacketReceived(const quic::QuicReceivedPacket& packet) override {
    for (Network::QuicListenerFilterPtr& accept_filter : accept_filters_) {
      Network::FilterStatus status = accept_filter->onFirstPacketReceived(packet);
      if (status == Network::FilterStatus::StopIteration) {
        return;
      }
    }
  }

private:
  Network::ConnectionSocket& socket_;
  StreamInfo::StreamInfo& stream_info_;
  Event::Dispatcher& dispatcher_;
  std::list<Network::QuicListenerFilterPtr> accept_filters_;
};

class EnvoyQuicServerConnection : public quic::QuicConnection, public QuicNetworkConnection {
public:
  EnvoyQuicServerConnection(const quic::QuicConnectionId& server_connection_id,
                            quic::QuicSocketAddress initial_self_address,
                            quic::QuicSocketAddress initial_peer_address,
                            quic::QuicConnectionHelperInterface& helper,
                            quic::QuicAlarmFactory& alarm_factory, quic::QuicPacketWriter* writer,
                            bool owns_writer,
                            const quic::ParsedQuicVersionVector& supported_versions,
                            Network::ConnectionSocketPtr connection_socket,
                            quic::ConnectionIdGeneratorInterface& generator,
                            std::unique_ptr<QuicListenerFilterManagerImpl> listener_filter_manager);

  // quic::QuicConnection
  // Overridden to set connection_socket_ with initialized self address and retrieve filter chain.
  bool OnPacketHeader(const quic::QuicPacketHeader& header) override;
  void OnCanWrite() override;
  void ProcessUdpPacket(const quic::QuicSocketAddress& self_address,
                        const quic::QuicSocketAddress& peer_address,
                        const quic::QuicReceivedPacket& packet) override;

  bool actuallyDeferSend() const { return defer_send_in_response_to_packets(); }

protected:
  void OnEffectivePeerMigrationValidated(bool is_migration_linkable) override;

private:
  std::unique_ptr<QuicListenerFilterManagerImpl> listener_filter_manager_;
  bool first_packet_received_ = false;
};

// An implementation that issues connection IDs with stable first 4 types.
class EnvoyQuicSelfIssuedConnectionIdManager : public quic::QuicSelfIssuedConnectionIdManager {
public:
  using QuicSelfIssuedConnectionIdManager::QuicSelfIssuedConnectionIdManager;
};

} // namespace Quic
} // namespace Envoy
