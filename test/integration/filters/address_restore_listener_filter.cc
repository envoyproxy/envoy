

#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/server/filter_config.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"

namespace Envoy {

// The FakeOriginalDstListenerFilter restore desired local address without the dependency of OS.
// Ipv6 and Ipv4 addresses are restored to the corresponding loopback ip address and port 80.
class FakeOriginalDstListenerFilter : public Network::ListenerFilter {
public:
  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override {
    ENVOY_LOG_MISC(debug, "in FakeOriginalDstListenerFilter::onAccept");
    Network::ConnectionSocket& socket = cb.socket();
    auto local_address = socket.connectionInfoProvider().localAddress();
    if (local_address != nullptr &&
        local_address->ip()->version() == Network::Address::IpVersion::v6) {
      socket.connectionInfoProvider().restoreLocalAddress(
          std::make_shared<Network::Address::Ipv6Instance>("::1", 80));
    } else {
      socket.connectionInfoProvider().restoreLocalAddress(
          std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 80));
    }
    ENVOY_LOG_MISC(debug, "current local socket address is {} restored = {}",
                   socket.connectionInfoProvider().localAddress()->asString(),
                   socket.connectionInfoProvider().localAddressRestored());
    return Network::FilterStatus::Continue;
  }

  size_t maxReadBytes() const override { return 0; }

  Network::FilterStatus onData(Network::ListenerFilterBuffer&) override {
    return Network::FilterStatus::Continue;
  }
};

class FakeOriginalDstListenerFilterConfigFactory
    : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message&,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext&) override {
    return [listener_filter_matcher](Network::ListenerFilterManager& filter_manager) -> void {
      filter_manager.addAcceptFilter(listener_filter_matcher,
                                     std::make_unique<FakeOriginalDstListenerFilter>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override {
    // This fake original_dest should be used only in integration test!
    return "envoy.filters.listener.original_dst";
  }
};

static Registry::RegisterFactory<FakeOriginalDstListenerFilterConfigFactory,
                                 Server::Configuration::NamedListenerFilterConfigFactory>
    register_;
} // namespace Envoy
