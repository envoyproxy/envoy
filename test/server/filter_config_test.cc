#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class TestHttpFilterConfigFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  TestHttpFilterConfigFactory() = default;

  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(std::make_shared<Http::PassThroughDecoderFilter>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override { return nullptr; }
  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override { return nullptr; }

  std::string name() const override { return "envoy.test.http_filter"; }
  std::string configType() override { return ""; };
};

TEST(NamedHttpFilterConfigFactoryTest, CreateFilterFactory) {
  TestHttpFilterConfigFactory factory;
  const std::string stats_prefix = "foo";
  Server::Configuration::MockFactoryContext context;
  ProtobufTypes::MessagePtr message{new Envoy::ProtobufWkt::Struct()};

  factory.createFilterFactoryFromProto(*message, stats_prefix, context);
}

} // namespace
} // namespace Envoy
