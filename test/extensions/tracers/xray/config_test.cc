#include "envoy/registry/registry.h"

#include "extensions/tracers/xray/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {
                TEST(XRayTracerConfigTest, XRayHttpTracer) {
                    NiceMock <Server::MockInstance> server;
                    const std::string yaml_string = R"EOF(
                      http:
                        name: envoy.xray
                        config:
                          segment_name: fake_name
                          daemon_endpoint: 127.0.0.1:2000
                      )EOF";

                    envoy::config::trace::v2::Tracing configuration;
                    MessageUtil::loadFromYaml(yaml_string, configuration);

                    XRayTracerFactory factory;
                    Tracing::HttpTracerPtr xray_tracer = factory.createHttpTracer(configuration, server);
                    EXPECT_NE(nullptr, xray_tracer);
                }

                TEST(XRayTracerConfigTest, XRayHttpTracerWithTypedConfig) {
                    NiceMock<Server::MockInstance> server;
                    const std::string yaml_string = R"EOF(
                      http:
                        name: envoy.xray
                        typed_config:
                          "@type": type.googleapis.com/envoy.config.trace.v2.XRayConfig
                          segment_name: fake_name
                          daemon_endpoint: 127.0.0.1:2000
                      )EOF";

                    envoy::config::trace::v2::Tracing configuration;
                    MessageUtil::loadFromYaml(yaml_string, configuration);

                    ZipkinTracerFactory factory;
                    auto message = Config::Utility::translateToFactoryConfig(configuration.http(), factory);
                    Tracing::HttpTracerPtr zipkin_tracer = factory.createHttpTracer(*message, server);
                    EXPECT_NE(nullptr, zipkin_tracer);
                }

                TEST(XRayTracerConfigTest, DoubleRegistrationTest) {
                    EXPECT_THROW_WITH_MESSAGE((Registry::RegisterFactory<XRayTracerFactory, Server::Configuration::TracerFactory>()),
                              EnvoyException, "Double registration for name: 'envoy.xray'");

                }
            } // namespace XRay
        } // namespace Tracers
    }// namespace Extensions
} // namespace Envoy
