#include <chrono>
#include <memory>
#include <string>

#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/runtime/uuid_util.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/xray/xray_core_constants.h"
#include "extensions/tracers/xray/xray_tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::Test;

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {
                class XRayDriverTest : public testing::Test {
                public:
                    XRayDriverTest() : time_source_(test_time_.timeSystem()) {}

                    void setup(envoy::config::trace::v2::XRayConfig& xray_config) {
                        driver_ = std::make_unique<Driver>(xray_config, tls_, runtime_, local_info_,
                                                           random_);
                    }

                    void setupValidDriver() {
                        const std::string yaml_string = R"EOF(
                            segment_name: fake_name
                            daemon_endpoint: 127.0.0.1:2000
                            )EOF";
                        envoy::config::trace::v2::XRayConfig xray_config;
                        MessageUtil::loadFromYaml(yaml_string, xray_config);

                        setup(xray_config);
                    }

                    uint64_t generateRandom64() { return Util::generateRandom64(time_source_); }

                    const std::string operation_name_{"test"};
                    Http::TestHeaderMapImpl request_headers_{
                            {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
                    SystemTime start_time_;
                    StreamInfo::MockStreamInfo stream_info_;

                    NiceMock<ThreadLocal::MockInstance> tls_;
                    std::unique_ptr<Driver> driver_;
                    NiceMock<Runtime::MockLoader> runtime_;
                    NiceMock<LocalInfo::MockLocalInfo> local_info_;
                    NiceMock<Runtime::MockRandomGenerator> random_;
                    NiceMock<Tracing::MockConfig> config_;
                    DangerousDeprecatedTestTime test_time_;
                    TimeSource& time_source_;
                };

                TEST_F(XRayDriverTest, InitializeDriver) {
                    {
                        // valid config
                        const std::string yaml_string = R"EOF(
                            segment_name: fake_name
                            daemon_endpoint: 127.0.0.1:2000
                            )EOF";
                        envoy::config::trace::v2::XRayConfig xray_config;
                        MessageUtil::loadFromYaml(yaml_string, xray_config);

                        setup(xray_config);
                    }
                }

                TEST_F(XRayDriverTest, XRaySpanContextFromXRayHeaderTest) {
                    setupValidDriver();

                    const std::string trace_header = "Root=1-5759e988-bd862e3fe1be46a994272793;Sampled=1";

                    request_headers_.insertXAmznTraceId().value(trace_header);

                    // New span will have an SR annotation - so its span and parent ids will be
                    // the same as the supplied span context (i.e. shared context)
                    Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                               start_time_, {Tracing::Reason::Sampling, true});

                    XRaySpanPtr xray_span(dynamic_cast<XRaySpan*>(span.release()));

                    EXPECT_EQ("1-5759e988-bd862e3fe1be46a994272793", xray_span->span().traceId());
                    EXPECT_TRUE(xray_span->span().sampled());
                }

            } //XRay
        } //Tracers
    } //Extensions
} //Envoy
