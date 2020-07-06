#include "extensions/filters/http/compressor/config.h"

#include "test/mocks/server/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {
namespace {

using testing::NiceMock;

TEST(CompressorFilterFactoryTests, MissingCompressorLibraryConfig) {
  const envoy::extensions::filters::http::compressor::v3::Compressor proto_config;
  CompressorFilterFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_MESSAGE(factory.createFilterFactoryFromProto(proto_config, "stats", context),
                            EnvoyException,
                            "Compressor filter doesn't have compressor_library defined");
}

TEST(CompressorFilterFactoryTests, UnregisteredCompressorLibraryConfig) {
  const std::string yaml_string = R"EOF(
  compressor_library:
    name: fake_compressor
    typed_config:
      "@type": type.googleapis.com/test.mock_compressor_library.Unregistered
  )EOF";

  envoy::extensions::filters::http::compressor::v3::Compressor proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  CompressorFilterFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_MESSAGE(factory.createFilterFactoryFromProto(proto_config, "stats", context),
                            EnvoyException,
                            "Didn't find a registered implementation for type: "
                            "'test.mock_compressor_library.Unregistered'");
}

} // namespace
} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
