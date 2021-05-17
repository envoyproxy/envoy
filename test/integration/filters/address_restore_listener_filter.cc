

#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/server/filter_config.h"

#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Envoy {

// The FakeOriginalDstListenerFilter restore desired local address without the dependency of OS.
class FakeOriginalDstListenerFilter : public Network::ListenerFilter {
public:
  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override {
    FANCY_LOG(debug, "in FakeOriginalDstListenerFilter::onAccept");
    Network::ConnectionSocket& socket = cb.socket();
    socket.addressProvider().restoreLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.2", 80));
    FANCY_LOG(debug, "current local socket address is {} restored = {}",
              socket.addressProvider().localAddress()->asString(),
              socket.addressProvider().localAddressRestored());
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
