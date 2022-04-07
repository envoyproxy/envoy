#include "test/integration/filters/test_listener_filter.h"

namespace Envoy {

/**
 * Config registration for the test filter.
 */
class TestInspectorConfigFactory : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message& /*message*/,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext& /*context*/) override {
    return [listener_filter_matcher](Network::ListenerFilterManager& filter_manager) -> void {
      filter_manager.addAcceptFilter(listener_filter_matcher,
                                     std::make_unique<TestListenerFilter>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { return "envoy.filters.listener.test"; }
};

absl::Mutex TestListenerFilter::alpn_lock_;
std::string TestListenerFilter::alpn_;

REGISTER_FACTORY(TestInspectorConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory){"envoy.listener.test"};

} // namespace Envoy
