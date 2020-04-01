#include "common/tracing/http_tracer_config_impl.h"
#include "common/tracing/http_tracer_impl.h"
#include "common/tracing/http_tracer_manager_impl.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::NotNull;
using testing::WhenDynamicCastTo;

namespace Envoy {
namespace Tracing {
namespace {

class SampleTracer : public HttpTracer {
public:
  SpanPtr startSpan(const Config&, Http::RequestHeaderMap&, const StreamInfo::StreamInfo&,
                    const Tracing::Decision) override {
    return nullptr;
  }
};

class SampleTracerFactory : public Server::Configuration::TracerFactory {
public:
  Tracing::HttpTracerSharedPtr
  createHttpTracer(const Protobuf::Message&,
                   Server::Configuration::TracerFactoryContext&) override {
    return std::make_shared<SampleTracer>();
  }

  std::string name() const override { return "envoy.tracers.sample"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }
};

class HttpTracerManagerImplTest : public testing::Test {
public:
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  HttpTracerManagerImpl http_tracer_manager_{std::make_unique<TracerFactoryContextImpl>(
      server_factory_context_, ProtobufMessage::getStrictValidationVisitor())};

private:
  SampleTracerFactory sample_tracer_factory_;
  Registry::InjectFactory<Server::Configuration::TracerFactory> registered_sample_tracer_factory_{
      sample_tracer_factory_};
};

TEST_F(HttpTracerManagerImplTest,
       ShouldReturnHttpNullTracerWhenNoTracingProviderHasBeenConfigured) {
  auto http_tracer = http_tracer_manager_.getOrCreateHttpTracer(nullptr);

  // Should return a null object (Tracing::HttpNullTracer) rather than nullptr.
  EXPECT_THAT(http_tracer.get(), WhenDynamicCastTo<Tracing::HttpNullTracer*>(NotNull()));
}

TEST_F(HttpTracerManagerImplTest, ShouldUseProperTracerFactory) {
  envoy::config::trace::v3::Tracing_Http tracing_config;
  tracing_config.set_name("envoy.tracers.sample");

  auto http_tracer = http_tracer_manager_.getOrCreateHttpTracer(&tracing_config);

  // Should use proper TracerFactory.
  EXPECT_THAT(http_tracer.get(), WhenDynamicCastTo<SampleTracer*>(NotNull()));
}

TEST_F(HttpTracerManagerImplTest, ShouldCacheAndReuseTracers) {
  envoy::config::trace::v3::Tracing_Http tracing_config;
  tracing_config.set_name("envoy.tracers.sample");
  tracing_config.mutable_typed_config()->PackFrom(MessageUtil::keyValueStruct("key1", "value1"));

  auto http_tracer_one = http_tracer_manager_.getOrCreateHttpTracer(&tracing_config);
  auto http_tracer_two = http_tracer_manager_.getOrCreateHttpTracer(&tracing_config);

  // Should reuse previously created HttpTracer instance.
  EXPECT_EQ(http_tracer_two, http_tracer_one);
}

TEST_F(HttpTracerManagerImplTest, ShouldCacheTracersBasedOnFullConfig) {
  envoy::config::trace::v3::Tracing_Http tracing_config_one;
  tracing_config_one.set_name("envoy.tracers.sample");
  tracing_config_one.mutable_typed_config()->PackFrom(
      MessageUtil::keyValueStruct("key1", "value1"));

  auto http_tracer_one = http_tracer_manager_.getOrCreateHttpTracer(&tracing_config_one);

  envoy::config::trace::v3::Tracing_Http tracing_config_two;
  tracing_config_two.set_name("envoy.tracers.sample");
  tracing_config_two.mutable_typed_config()->PackFrom(
      MessageUtil::keyValueStruct("key2", "value2"));

  auto http_tracer_two = http_tracer_manager_.getOrCreateHttpTracer(&tracing_config_two);

  // Any changes to config must result in a new HttpTracer instance.
  EXPECT_NE(http_tracer_two, http_tracer_one);
}

TEST_F(HttpTracerManagerImplTest, ShouldFailIfTracerProviderIsUnknown) {
  envoy::config::trace::v3::Tracing_Http tracing_config;
  tracing_config.set_name("invalid");

  EXPECT_THROW_WITH_MESSAGE(http_tracer_manager_.getOrCreateHttpTracer(&tracing_config),
                            EnvoyException,
                            "Didn't find a registered implementation for name: 'invalid'");
}

TEST_F(HttpTracerManagerImplTest, ShouldFailIfProviderSpecificConfigIsNotValid) {
  envoy::config::trace::v3::Tracing_Http tracing_config;
  tracing_config.set_name("envoy.tracers.sample");
  tracing_config.mutable_typed_config()->PackFrom(ValueUtil::stringValue("value"));

  EXPECT_THROW_WITH_MESSAGE(
      http_tracer_manager_.getOrCreateHttpTracer(&tracing_config), EnvoyException,
      R"(Unable to unpack as google.protobuf.Struct: [type.googleapis.com/google.protobuf.Value] {
  string_value: "value"
}
)");
}

} // namespace
} // namespace Tracing
} // namespace Envoy
