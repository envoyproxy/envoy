#include "source/common/tracing/http_tracer_impl.h"
#include "source/common/tracing/http_tracer_manager_impl.h"
#include "source/common/tracing/tracer_config_impl.h"

#include "test/mocks/server/instance.h"
#include "test/mocks/server/tracer_factory.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::NotNull;
using testing::SizeIs;
using testing::WhenDynamicCastTo;

namespace Envoy {
namespace Tracing {
namespace {

class SampleDriver : public Driver {
public:
  SpanPtr startSpan(const Config&, Tracing::TraceContext&, const std::string&, SystemTime,
                    const Tracing::Decision) override {
    return nullptr;
  }
};

class SampleTracerFactory : public Server::Configuration::TracerFactory {
public:
  Tracing::DriverSharedPtr
  createTracerDriver(const Protobuf::Message&,
                     Server::Configuration::TracerFactoryContext&) override {
    return std::make_shared<SampleDriver>();
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

  EXPECT_THAT(http_tracer.get(), WhenDynamicCastTo<HttpTracerImpl*>(NotNull()));
  auto http_tracer_impl = dynamic_cast<HttpTracerImpl*>(http_tracer.get());

  // Should use proper TracerFactory.
  EXPECT_THAT(http_tracer_impl->driverForTest().get(), WhenDynamicCastTo<SampleDriver*>(NotNull()));
}

TEST_F(HttpTracerManagerImplTest, ShouldCacheAndReuseTracers) {
  envoy::config::trace::v3::Tracing_Http tracing_config;
  tracing_config.set_name("envoy.tracers.sample");
  tracing_config.mutable_typed_config()->PackFrom(MessageUtil::keyValueStruct("key1", "value1"));

  auto http_tracer_one = http_tracer_manager_.getOrCreateHttpTracer(&tracing_config);
  // Expect a new HttpTracer to be added to the cache.
  EXPECT_THAT(http_tracer_manager_.peekCachedTracersForTest(), SizeIs(1));

  auto http_tracer_two = http_tracer_manager_.getOrCreateHttpTracer(&tracing_config);
  // Expect no changes to the cache.
  EXPECT_THAT(http_tracer_manager_.peekCachedTracersForTest(), SizeIs(1));

  // Should reuse previously created HttpTracer instance.
  EXPECT_EQ(http_tracer_two, http_tracer_one);
}

TEST_F(HttpTracerManagerImplTest, ShouldCacheTracersBasedOnFullConfig) {
  envoy::config::trace::v3::Tracing_Http tracing_config_one;
  tracing_config_one.set_name("envoy.tracers.sample");
  tracing_config_one.mutable_typed_config()->PackFrom(
      MessageUtil::keyValueStruct("key1", "value1"));

  auto http_tracer_one = http_tracer_manager_.getOrCreateHttpTracer(&tracing_config_one);
  // Expect a new HttpTracer to be added to the cache.
  EXPECT_THAT(http_tracer_manager_.peekCachedTracersForTest(), SizeIs(1));

  envoy::config::trace::v3::Tracing_Http tracing_config_two;
  tracing_config_two.set_name("envoy.tracers.sample");
  tracing_config_two.mutable_typed_config()->PackFrom(
      MessageUtil::keyValueStruct("key2", "value2"));

  auto http_tracer_two = http_tracer_manager_.getOrCreateHttpTracer(&tracing_config_two);
  // Expect a new HttpTracer to be added to the cache.
  EXPECT_THAT(http_tracer_manager_.peekCachedTracersForTest(), SizeIs(2));

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

class HttpTracerManagerImplCacheTest : public testing::Test {
public:
  HttpTracerManagerImplCacheTest() {
    tracing_config_one_.set_name("envoy.tracers.mock");
    tracing_config_one_.mutable_typed_config()->PackFrom(
        MessageUtil::keyValueStruct("key1", "value1"));

    tracing_config_two_.set_name("envoy.tracers.mock");
    tracing_config_two_.mutable_typed_config()->PackFrom(
        MessageUtil::keyValueStruct("key2", "value2"));
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  HttpTracerManagerImpl http_tracer_manager_{std::make_unique<TracerFactoryContextImpl>(
      server_factory_context_, ProtobufMessage::getStrictValidationVisitor())};

  NiceMock<Server::Configuration::MockTracerFactory> tracer_factory_{"envoy.tracers.mock"};

  envoy::config::trace::v3::Tracing_Http tracing_config_one_;
  envoy::config::trace::v3::Tracing_Http tracing_config_two_;

private:
  Registry::InjectFactory<Server::Configuration::TracerFactory> registered_tracer_factory_{
      tracer_factory_};
};

TEST_F(HttpTracerManagerImplCacheTest, ShouldCacheHttpTracersUsingWeakReferences) {
  Driver* expected_driver = new NiceMock<MockDriver>();

  // Expect HttpTracerManager to create a new HttpTracer.
  EXPECT_CALL(tracer_factory_, createTracerDriver(_, _))
      .WillOnce(InvokeWithoutArgs(
          [expected_driver] { return std::shared_ptr<Driver>(expected_driver); }));

  auto actual_tracer_one = http_tracer_manager_.getOrCreateHttpTracer(&tracing_config_one_);

  EXPECT_EQ(dynamic_cast<HttpTracerImpl*>(actual_tracer_one.get())->driverForTest().get(),
            expected_driver);
  // Expect a new HttpTracer to be added to the cache.
  EXPECT_THAT(http_tracer_manager_.peekCachedTracersForTest(), SizeIs(1));

  // Expect HttpTracerManager to re-use cached value.
  auto actual_tracer_two = http_tracer_manager_.getOrCreateHttpTracer(&tracing_config_one_);

  EXPECT_EQ(dynamic_cast<HttpTracerImpl*>(actual_tracer_one.get())->driverForTest().get(),
            expected_driver);
  // Expect no changes to the cache.
  EXPECT_THAT(http_tracer_manager_.peekCachedTracersForTest(), SizeIs(1));

  // Expect HttpTracerManager to use weak references under the hood and release HttpTracer as soon
  // as it's no longer in use.
  std::weak_ptr<HttpTracer> weak_pointer{actual_tracer_one};

  actual_tracer_one.reset();
  // Expect one strong reference still to be left.
  EXPECT_NE(weak_pointer.lock(), nullptr);

  actual_tracer_two.reset();
  // Expect no more strong references to be left.
  EXPECT_EQ(weak_pointer.lock(), nullptr);

  Driver* expected_other_driver = new NiceMock<MockDriver>();

  // Expect HttpTracerManager to create a new HttpTracer once again.
  EXPECT_CALL(tracer_factory_, createTracerDriver(_, _))
      .WillOnce(InvokeWithoutArgs(
          [expected_other_driver] { return std::shared_ptr<Driver>(expected_other_driver); }));

  // Use a different config to guarantee that a new cache entry will be added anyway.
  auto actual_tracer_three = http_tracer_manager_.getOrCreateHttpTracer(&tracing_config_two_);

  EXPECT_EQ(dynamic_cast<HttpTracerImpl*>(actual_tracer_three.get())->driverForTest().get(),
            expected_other_driver);
  // Expect expired cache entries to be removed and a new HttpTracer to be added to the cache.
  EXPECT_THAT(http_tracer_manager_.peekCachedTracersForTest(), SizeIs(1));

  // Expect HttpTracerManager to keep the right value in the cache.
  auto actual_tracer_four = http_tracer_manager_.getOrCreateHttpTracer(&tracing_config_two_);

  EXPECT_EQ(dynamic_cast<HttpTracerImpl*>(actual_tracer_four.get())->driverForTest().get(),
            expected_other_driver);
  // Expect no changes to the cache.
  EXPECT_THAT(http_tracer_manager_.peekCachedTracersForTest(), SizeIs(1));
}

} // namespace
} // namespace Tracing
} // namespace Envoy
