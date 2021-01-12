#include "envoy/http/filter.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/filter_config.h"

#include "common/http/match_wrapper/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Common {
namespace Http {
namespace MatchWrapper {
namespace {

struct TestFactory : public Envoy::Server::Configuration::NamedHttpFilterConfigFactory {
  std::string name() const override { return "test"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
  Envoy::Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::FactoryContext&) override {
    return [](auto& callbacks) {
      callbacks.addStreamDecoderFilter(nullptr);
      callbacks.addStreamEncoderFilter(nullptr);
      callbacks.addStreamFilter(nullptr);

      callbacks.addStreamDecoderFilter(nullptr, nullptr);
      callbacks.addStreamEncoderFilter(nullptr, nullptr);
      callbacks.addStreamFilter(nullptr, nullptr);

      callbacks.addAccessLogHandler(nullptr);
    };
  }
};

TEST(MatchWrapper, WithMatcher) {
  TestFactory test_factory;
  Envoy::Registry::InjectFactory<Envoy::Server::Configuration::NamedHttpFilterConfigFactory>
      inject_factory(test_factory);

  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;

  const auto config =
      TestUtility::parseYaml<envoy::extensions::common::matching::v3::ExtensionWithMatcher>(R"EOF(
extension_config:
  name: test
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
matcher:
  matcher_tree:
    input:
      name: request-headers
      typed_config:
        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
        header_name: default-matcher-header
    exact_match_map:
        map:
            match:
                action:
                    name: skip
                    typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
)EOF");

  MatchWrapperConfig match_wrapper_config;
  auto cb = match_wrapper_config.createFilterFactoryFromProto(config, "", factory_context);

  Envoy::Http::MockFilterChainFactoryCallbacks factory_callbacks;
  testing::InSequence s;

  // This matches the sequence of calls in the filter factory above: the ones that call the overload
  // without a match tree has a match tree added, the other one does not.
  EXPECT_CALL(factory_callbacks, addStreamDecoderFilter(_, testing::NotNull()));
  EXPECT_CALL(factory_callbacks, addStreamEncoderFilter(_, testing::NotNull()));
  EXPECT_CALL(factory_callbacks, addStreamFilter(_, testing::NotNull()));
  EXPECT_CALL(factory_callbacks, addStreamDecoderFilter(_, testing::IsNull()));
  EXPECT_CALL(factory_callbacks, addStreamEncoderFilter(_, testing::IsNull()));
  EXPECT_CALL(factory_callbacks, addStreamFilter(_, testing::IsNull()));
  EXPECT_CALL(factory_callbacks, addAccessLogHandler(_));
  cb(factory_callbacks);
}

} // namespace
} // namespace MatchWrapper
} // namespace Http
} // namespace Common
} // namespace Envoy