#include "test/integration/filters/test_listener_filter.h"

#include "test/integration/filters/test_listener_filter.pb.h"

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
    return std::make_unique<test::integration::filters::TestInspectorFilterConfig>();
  }

  std::string name() const override { return "envoy.filters.listener.test"; }
};

absl::Mutex TestListenerFilter::alpn_lock_;
std::string TestListenerFilter::alpn_;

/**
 * Config registration for the UDP test filter.
 */
class TestUdpInspectorConfigFactory
    : public Server::Configuration::NamedUdpListenerFilterConfigFactory {
public:
  // NamedUdpListenerFilterConfigFactory
  Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::ListenerFactoryContext&) override {
    return [](Network::UdpListenerFilterManager& filter_manager,
              Network::UdpReadFilterCallbacks& callbacks) -> void {
      filter_manager.addReadFilter(std::make_unique<TestUdpListenerFilter>(callbacks));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::integration::filters::TestUdpListenerFilterConfig>();
  }

  std::string name() const override { return "envoy.filters.udp_listener.test"; }
};

REGISTER_FACTORY(TestInspectorConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory){"envoy.listener.test"};

REGISTER_FACTORY(TestUdpInspectorConfigFactory,
                 Server::Configuration::NamedUdpListenerFilterConfigFactory){"envoy.listener.test"};
} // namespace Envoy
