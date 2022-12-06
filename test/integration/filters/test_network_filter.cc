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

} // namespace
} // namespace Envoy
