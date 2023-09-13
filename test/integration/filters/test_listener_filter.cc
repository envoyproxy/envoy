#include "test/integration/filters/test_listener_filter.h"

#include "test/integration/filters/test_listener_filter.pb.h"
#include "test/integration/filters/test_listener_filter.pb.validate.h"

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
 * Config registration for the test TCP listener filter.
 */
class TestTcpInspectorConfigFactory
    : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message& proto_config,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext& context) override {
    const auto& message = MessageUtil::downcastAndValidate<
        const test::integration::filters::TestTcpListenerFilterConfig&>(
        proto_config, context.messageValidationVisitor());
    return [listener_filter_matcher,
            message](Network::ListenerFilterManager& filter_manager) -> void {
      filter_manager.addAcceptFilter(
          listener_filter_matcher, std::make_unique<TestTcpListenerFilter>(message.drain_bytes()));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::integration::filters::TestTcpListenerFilterConfig>();
  }

  std::string name() const override { return "envoy.filters.tcp_listener.test"; }
};

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

#ifdef ENVOY_ENABLE_QUIC
/**
 * Config registration for the test filter.
 */
class TestQuicListenerFilterConfigFactory
    : public Server::Configuration::NamedQuicListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::QuicListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message& proto_config,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext& context) override {
    const auto& message = MessageUtil::downcastAndValidate<
        const test::integration::filters::TestQuicListenerFilterConfig&>(
        proto_config, context.messageValidationVisitor());
    return [listener_filter_matcher, added_value = message.added_value(),
            allow_server_migration = message.allow_server_migration(),
            allow_client_migration = message.allow_client_migration()](
               Network::QuicListenerFilterManager& filter_manager) -> void {
      filter_manager.addFilter(listener_filter_matcher,
                               std::make_unique<TestQuicListenerFilter>(
                                   added_value, allow_server_migration, allow_client_migration));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::integration::filters::TestQuicListenerFilterConfig>();
  }

  std::string name() const override { return "envoy.filters.quic_listener.test"; }
};

REGISTER_FACTORY(TestQuicListenerFilterConfigFactory,
                 Server::Configuration::NamedQuicListenerFilterConfigFactory);

#endif

REGISTER_FACTORY(TestInspectorConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory);

REGISTER_FACTORY(TestTcpInspectorConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory);

REGISTER_FACTORY(TestUdpInspectorConfigFactory,
                 Server::Configuration::NamedUdpListenerFilterConfigFactory);

} // namespace Envoy
