#include "envoy/server/filter_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/mocks/server/factory_context.h"

namespace Envoy {
namespace {

class TestHttpFilter : public Http::StreamDecoderFilter {
 public:
  TestHttpFilter() {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Envoy::Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Envoy::Http::FilterTrailersStatus::Continue;
  }

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks&) override {}
  void onDestroy() override {};
};

class TestHttpFilterConfigFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  TestHttpFilterConfigFactory() {}

  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(std::make_shared<TestHttpFilter>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override { return nullptr; }
  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override {
    return nullptr;
  }

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
