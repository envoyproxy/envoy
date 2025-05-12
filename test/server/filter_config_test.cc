#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace Configuration {
namespace {

using envoy::extensions::filters::common::dependency::v3::Dependency;
using envoy::extensions::filters::common::dependency::v3::FilterDependencies;

class TestHttpFilterConfigFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  TestHttpFilterConfigFactory() = default;

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(std::make_shared<Http::PassThroughDecoderFilter>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override { return nullptr; }
  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override { return nullptr; }

  FilterDependenciesPtr dependencies() override {
    FilterDependencies dependencies;
    Dependency* d = dependencies.add_decode_required();
    d->set_name("foobar");
    d->set_type(Dependency::FILTER_STATE_KEY);
    return std::make_unique<FilterDependencies>(dependencies);
  }

  std::string name() const override { return "envoy.test.http_filter"; }
  std::set<std::string> configTypes() override { return {}; };
};

TEST(NamedHttpFilterConfigFactoryTest, CreateFilterFactory) {
  TestHttpFilterConfigFactory factory;
  const std::string stats_prefix = "foo";
  Server::Configuration::MockFactoryContext context;
  ProtobufTypes::MessagePtr message{new Envoy::ProtobufWkt::Struct()};

  EXPECT_TRUE(factory.createFilterFactoryFromProto(*message, stats_prefix, context).status().ok());
}

TEST(NamedHttpFilterConfigFactoryTest, Dependencies) {
  TestHttpFilterConfigFactory factory;
  const std::string stats_prefix = "foo";
  Server::Configuration::MockFactoryContext context;
  ProtobufTypes::MessagePtr message{new Envoy::ProtobufWkt::Struct()};

  EXPECT_TRUE(factory.createFilterFactoryFromProto(*message, stats_prefix, context).status().ok());

  EXPECT_EQ(factory.dependencies()->decode_required().size(), 1);
}

} // namespace
} // namespace Configuration
} // namespace Server
} // namespace Envoy
