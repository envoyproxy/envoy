#include "source/extensions/filters/http/compressor/config.h"

#include "test/extensions/filters/http/compressor/mock_compressor_library.pb.h"
#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {
namespace {

using testing::NiceMock;

const ::test::mock_compressor_library::Unregistered _mock_compressor_library_dummy;

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
