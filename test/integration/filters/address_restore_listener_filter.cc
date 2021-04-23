

#include "envoy/network/filter.h"

#include "common/network/address_impl.h"
#include "envoy/network/listen_socket.h"
#include "common/network/utility.h"
#include "envoy/server/filter_config.h"

namespace Envoy {

class FakeListenerFilter : public Network::ListenerFilter {
public:
  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override {
    FANCY_LOG(info, "calling FakeListenerFilter::onAccept");
    Network::ConnectionSocket& socket = cb.socket();
    FANCY_LOG(info, "lambdai: current local socket address is {} restored = {}",
              socket.addressProvider().localAddress()->asString(),
              socket.addressProvider().localAddressRestored());
    FANCY_LOG(info, "lambdai: current remote socket address is {}",
              socket.addressProvider().remoteAddress()->asString());
    socket.addressProvider().restoreLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.2", 80));
    FANCY_LOG(info, "lambdai: current local socket address is {} restored = {}",
              socket.addressProvider().localAddress()->asString(),
              socket.addressProvider().localAddressRestored());
    FANCY_LOG(info, "lambdai: current remote socket address is {}",
              socket.addressProvider().remoteAddress()->asString());
    return Network::FilterStatus::Continue;
  }
};

class FakeListenerFilterConfigFactory
    : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message&,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext&) override {
    return [listener_filter_matcher](Network::ListenerFilterManager& filter_manager) -> void {
      filter_manager.addAcceptFilter(listener_filter_matcher,
                                     std::make_unique<FakeListenerFilter>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { return "envoy.listener.test_address_restore"; }
};

static Registry::RegisterFactory<FakeListenerFilterConfigFactory,
                                 Server::Configuration::NamedListenerFilterConfigFactory>
    register_;

// REGISTER_FACTORY(FakeListenerFilterConfigFactory,
//                  Server::Configuration::NamedListenerFilterConfigFactory){
//     "envoy.listener.test_address_restore"};
} // namespace Envoy