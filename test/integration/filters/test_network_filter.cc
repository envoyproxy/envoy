#include "envoy/network/filter.h"

#include "source/extensions/filters/network/common/factory_base.h"

#include "test/integration/filters/test_network_filter.pb.h"
#include "test/integration/filters/test_network_filter.pb.validate.h"

namespace Envoy {
namespace {

/**
 * All stats for this filter. @see stats_macros.h
 */
#define ALL_TEST_NETWORK_FILTER_STATS(COUNTER)                                                     \
  COUNTER(on_new_connection)                                                                       \
  COUNTER(on_data)

/**
 * Struct definition for stats. @see stats_macros.h
 */
struct TestNetworkFilterStats {
  ALL_TEST_NETWORK_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

class TestNetworkFilter : public Network::ReadFilter {
public:
  TestNetworkFilter(Stats::Scope& scope) : stats_(generateStats("test_network_filter", scope)) {}

  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    stats_.on_data_.inc();
    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onNewConnection() override {
    stats_.on_new_connection_.inc();
    return Network::FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  Envoy::Network::ReadFilterCallbacks* read_callbacks_{};
  TestNetworkFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return {ALL_TEST_NETWORK_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }
  TestNetworkFilterStats stats_;
};

class TestNetworkFilterConfigFactory : public Extensions::NetworkFilters::Common::FactoryBase<
                                           test::integration::filters::TestNetworkFilterConfig> {
public:
  TestNetworkFilterConfigFactory()
      : Extensions::NetworkFilters::Common::FactoryBase<
            test::integration::filters::TestNetworkFilterConfig>("envoy.test.test_network_filter") {
  }

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::TestNetworkFilterConfig& config,
      Server::Configuration::FactoryContext& context) override {
    return [config, &context](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<TestNetworkFilter>(context.scope()));
    };
  }
};

// perform static registration
static Registry::RegisterFactory<TestNetworkFilterConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    register_;

class TestDrainerNetworkFilter : public Network::ReadFilter {
public:
  TestDrainerNetworkFilter(const test::integration::filters::TestDrainerNetworkFilterConfig& config)
      : bytes_to_drain_(config.bytes_to_drain()) {}

  Network::FilterStatus onData(Buffer::Instance& buffer, bool) override {
    buffer.drain(bytes_to_drain_);
    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  Envoy::Network::ReadFilterCallbacks* read_callbacks_{};
  int bytes_to_drain_;
};

class TestDrainerNetworkFilterConfigFactory
    : public Extensions::NetworkFilters::Common::FactoryBase<
          test::integration::filters::TestDrainerNetworkFilterConfig> {
public:
  TestDrainerNetworkFilterConfigFactory()
      : Extensions::NetworkFilters::Common::FactoryBase<
            test::integration::filters::TestDrainerNetworkFilterConfig>(
            "envoy.test.test_drainer_network_filter") {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::TestDrainerNetworkFilterConfig& config,
      Server::Configuration::FactoryContext&) override {
    return [config](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<TestDrainerNetworkFilter>(config));
    };
  }

  bool isTerminalFilterByProtoTyped(
      const test::integration::filters::TestDrainerNetworkFilterConfig& config,
      Server::Configuration::ServerFactoryContext&) override {
    return config.is_terminal_filter();
  }
};

static Registry::RegisterFactory<TestDrainerNetworkFilterConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    drainer_register_;

class TestDrainerUpstreamNetworkFilter : public Network::WriteFilter {
public:
  TestDrainerUpstreamNetworkFilter(
      const test::integration::filters::TestDrainerUpstreamNetworkFilterConfig& config)
      : bytes_to_drain_(config.bytes_to_drain()) {}

  Network::FilterStatus onWrite(Buffer::Instance& buffer, bool) override {
    buffer.drain(bytes_to_drain_);
    return Network::FilterStatus::Continue;
  }

  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

private:
  Envoy::Network::WriteFilterCallbacks* write_callbacks_{};
  int bytes_to_drain_;
};

class TestDrainerUpstreamNetworkFilterConfigFactory
    : public Server::Configuration::NamedUpstreamNetworkFilterConfigFactory {
public:
  std::string name() const override { return "envoy.test.test_drainer_upstream_network_filter"; }

  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::UpstreamFactoryContext& context) override {
    const auto& config = MessageUtil::downcastAndValidate<
        const test::integration::filters::TestDrainerUpstreamNetworkFilterConfig&>(
        proto_config, context.serverFactoryContext().messageValidationVisitor());
    return [config](Network::FilterManager& filter_manager) -> void {
      filter_manager.addWriteFilter(std::make_shared<TestDrainerUpstreamNetworkFilter>(config));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::integration::filters::TestDrainerUpstreamNetworkFilterConfig>();
  }
};

static Registry::RegisterFactory<TestDrainerUpstreamNetworkFilterConfigFactory,
                                 Server::Configuration::NamedUpstreamNetworkFilterConfigFactory>
    upstream_drainer_register_;

} // namespace
} // namespace Envoy
