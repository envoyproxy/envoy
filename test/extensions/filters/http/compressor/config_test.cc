#include "envoy/compression/compressor/config.h"
#include "envoy/compression/compressor/factory.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/listener.h"

#include "source/extensions/filters/http/compressor/config.h"

#include "test/extensions/filters/http/compressor/mock_compressor_library.pb.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

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
  EXPECT_THAT(
      factory.createFilterFactoryFromProto(proto_config, "stats", context).status().message(),
      testing::HasSubstr("Didn't find a registered implementation for type: "
                         "'test.mock_compressor_library.Unregistered'"));
}

// Minimal no-op compressor factory to inject and validate registered path.
class TestNoopCompressorFactory : public Envoy::Compression::Compressor::CompressorFactory {
public:
  Envoy::Compression::Compressor::CompressorPtr createCompressor() override {
    return nullptr; // not used
  }
  const std::string& statsPrefix() const override {
    static const std::string p{"test_noop."};
    return p;
  }
  const std::string& contentEncoding() const override {
    static const std::string e{"noop"};
    return e;
  }
};

class TestNoopCompressorLibraryFactory
    : public Envoy::Compression::Compressor::NamedCompressorLibraryConfigFactory {
public:
  TestNoopCompressorLibraryFactory() = default;

private:
  Envoy::Compression::Compressor::CompressorFactoryPtr
  createCompressorFactoryFromProto(const Protobuf::Message& /*config*/,
                                   Server::Configuration::FactoryContext& /*context*/) override {
    return std::make_unique<TestNoopCompressorFactory>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<::test::mock_compressor_library::Registered>();
  }

  std::string name() const override { return "test.mock.noop"; }
  std::string category() const override { return "envoy.compression.compressor"; }
};

TEST(CompressorFilterFactoryTests, RegisteredCompressorLibraryConfig) {
  const std::string yaml_string = R"EOF(
  compressor_library:
    name: test.mock.noop
    typed_config:
      "@type": type.googleapis.com/test.mock_compressor_library.Registered
  )EOF";

  envoy::extensions::filters::http::compressor::v3::Compressor proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  CompressorFilterFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  TestNoopCompressorLibraryFactory factory_impl;
  Envoy::Registry::InjectFactory<
      Envoy::Compression::Compressor::NamedCompressorLibraryConfigFactory>
      reg(factory_impl);
  auto cb_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(cb_or.status().ok());
}

// Factory that touches drainDecision() and listenerInfo() on FactoryContext to cover wrapper.
class TestCheckingCompressorLibraryFactory
    : public Envoy::Compression::Compressor::NamedCompressorLibraryConfigFactory {
public:
  TestCheckingCompressorLibraryFactory() = default;

  Envoy::Compression::Compressor::CompressorFactoryPtr
  createCompressorFactoryFromProto(const Protobuf::Message& /*config*/,
                                   Server::Configuration::FactoryContext& context) override {
    (void)context.serverFactoryContext();
    (void)context.messageValidationVisitor();
    (void)context.initManager();
    (void)context.scope();
    (void)context.listenerScope();
    (void)context.drainDecision().drainClose(Network::DrainDirection::All);
    (void)context.drainDecision().addOnDrainCloseCb(
        Network::DrainDirection::All, [](std::chrono::milliseconds) { return absl::OkStatus(); });
    const auto& info = context.listenerInfo();
    (void)info.metadata();
    (void)info.typedMetadata();
    (void)info.direction();
    (void)info.isQuic();
    (void)info.shouldBypassOverloadManager();

    return std::make_unique<TestNoopCompressorFactory>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<::test::mock_compressor_library::Registered>();
  }

  std::string name() const override { return "test.mock.check"; }
  std::string category() const override { return "envoy.compression.compressor"; }
};

TEST(CompressorFilterFactoryTests, PerRouteWrapperCoversDrainAndListenerInfo) {
  // Per-route config with a typed compressor_library using the checking factory.
  const std::string yaml_string = R"EOF(
  overrides:
    response_direction_config: {}
    compressor_library:
      name: test.mock.check
      typed_config:
        "@type": type.googleapis.com/test.mock_compressor_library.Registered
  )EOF";

  envoy::extensions::filters::http::compressor::v3::CompressorPerRoute per_route;
  TestUtility::loadFromYaml(yaml_string, per_route);
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  CompressorFilterFactory factory;
  TestCheckingCompressorLibraryFactory checking_impl;
  Envoy::Registry::InjectFactory<
      Envoy::Compression::Compressor::NamedCompressorLibraryConfigFactory>
      reg(checking_impl);

  auto cfg_or = factory.createRouteSpecificFilterConfig(per_route, context,
                                                        context.messageValidationVisitor());
  EXPECT_TRUE(cfg_or.status().ok());
}

TEST(CompressorFilterFactoryTests, EmptyPerRouteConfig) {
  envoy::extensions::filters::http::compressor::v3::CompressorPerRoute per_route;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  CompressorFilterFactory factory;
  EXPECT_THROW(
      factory
          .createRouteSpecificFilterConfig(per_route, context, context.messageValidationVisitor())
          .value(),
      ProtoValidationException);
}

TEST(CompressorFilterFactoryTests, PerRouteWrapperBuilds) {
  // Provide a minimally valid per-route proto: set overrides with empty response_direction_config
  envoy::extensions::filters::http::compressor::v3::CompressorPerRoute per_route;
  per_route.mutable_overrides()->mutable_response_direction_config();
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  CompressorFilterFactory factory;
  auto cfg_or = factory.createRouteSpecificFilterConfig(per_route, context,
                                                        context.messageValidationVisitor());
  EXPECT_TRUE(cfg_or.status().ok());
  // No further assertions; this exercises the GenericFactoryContext wrapper path.
}

} // namespace
} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
