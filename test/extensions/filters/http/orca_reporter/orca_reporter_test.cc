#include "envoy/extensions/filters/http/orca_reporter/v3/orca_reporter.pb.h"
#include "envoy/extensions/filters/http/orca_reporter/v3/orca_reporter.pb.validate.h"

#include "source/common/common/base64.h"
#include "source/common/common/regex.h"
#include "source/common/orca/orca_parser.h"
#include "source/extensions/filters/http/orca_reporter/orca_reporter.h"
#include "source/extensions/load_balancing_policies/common/orca_weight_manager.h"

#include "test/extensions/filters/http/orca_reporter/fake_cgroup.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OrcaReporter {
namespace {

using StatusHelpers::HasStatus;
using testing::_;
using testing::HasSubstr;
using testing::NiceMock;

const Http::LowerCaseString TextHeader{"endpoint-load-metrics"};
const Http::LowerCaseString BinaryHeader{"endpoint-load-metrics-bin"};

class OrcaReporterTest : public testing::Test {
protected:
  OrcaReporterTest() : api_(Api::createApiForTest()) {}

  absl::StatusOr<OrcaReporterConfigSharedPtr> create(const std::string& yaml) {
    ProtoConfig proto_config;
    TestUtility::loadFromYamlAndValidate(yaml, proto_config);
    absl::StatusOr<LoadReporterSharedPtr> reporter = registry_.getOrCreate(
        proto_config, api_->fileSystem(), regex_engine_, time_system_, dispatcher_, tls_,
        [this](const std::string& name) -> std::optional<std::string> {
          auto it = env_.find(name);
          if (it == env_.end()) {
            return std::nullopt;
          }
          return it->second;
        });
    RETURN_IF_NOT_OK_REF(reporter.status());
    return std::make_shared<OrcaReporterConfig>(proto_config, std::move(reporter).value());
  }

  void initialize(const std::string& yaml) {
    timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    absl::StatusOr<OrcaReporterConfigSharedPtr> config = create(yaml);
    ASSERT_TRUE(config.ok()) << config.status();
    config_ = std::move(config).value();
  }

  void refresh(std::chrono::milliseconds elapsed = std::chrono::seconds(1)) {
    time_system_.advanceTimeWait(elapsed);
    timer_->invokeCallback();
  }

  Http::TestResponseHeaderMapImpl encode(Http::TestResponseHeaderMapImpl headers = {
                                             {":status", "200"}}) {
    OrcaReporterFilter filter(config_);
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.encodeHeaders(headers, false));
    return headers;
  }

  // Encodes a response with a body and trailers.
  std::pair<Http::TestResponseHeaderMapImpl, Http::TestResponseTrailerMapImpl>
  encodeWithTrailers(Http::TestResponseHeaderMapImpl headers,
                     Http::TestResponseTrailerMapImpl trailers = {}) {
    OrcaReporterFilter filter(config_);
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.encodeHeaders(headers, false));
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter.encodeTrailers(trailers));
    return {std::move(headers), std::move(trailers)};
  }

  std::string reportHeader(const Http::LowerCaseString& name = TextHeader) {
    return std::string(encode().get_(name));
  }

  xds::data::orca::v3::OrcaLoadReport parsedReport() {
    absl::StatusOr<xds::data::orca::v3::OrcaLoadReport> report =
        Orca::parseOrcaLoadReportHeaders(encode());
    EXPECT_TRUE(report.ok()) << report.status();
    return report.value_or(xds::data::orca::v3::OrcaLoadReport());
  }

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Regex::GoogleReEngine regex_engine_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  LoadReporterRegistry registry_;
  Event::MockTimer* timer_{};
  absl::flat_hash_map<std::string, std::string> env_;
  FakeCgroup pod_{"pod"};
  FakeCgroup app_{"app"};
  OrcaReporterConfigSharedPtr config_;
};

std::string cpuAndMemoryConfig(absl::string_view path) {
  return absl::Substitute(R"EOF(
metrics:
- name: cpu_utilization
  cgroup_cpu:
    cgroups:
    - path: "$0"
- name: mem_utilization
  cgroup_memory:
    cgroups:
    - path: "$0"
)EOF",
                          path);
}

TEST_F(OrcaReporterTest, CpuAndMemoryFromCgroupLimits) {
  pod_.setCpuMax("200000 100000");
  pod_.setMemory(600, 100);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()));

  // CPU needs two samples.
  EXPECT_EQ("TEXT mem_utilization=0.5", reportHeader());

  // 1s of CPU time in 1s on 2 cores.
  pod_.setCpuUsage(1000000);
  pod_.setMemory(300, 50);
  refresh();
  EXPECT_EQ("TEXT cpu_utilization=0.5,mem_utilization=0.25", reportHeader());

  const xds::data::orca::v3::OrcaLoadReport report = parsedReport();
  EXPECT_DOUBLE_EQ(0.5, report.cpu_utilization());
  EXPECT_DOUBLE_EQ(0.25, report.mem_utilization());
}

TEST_F(OrcaReporterTest, ConfiguredCapacityOverridesLimits) {
  initialize(absl::Substitute(R"EOF(
metrics:
- name: cpu_utilization
  cgroup_cpu:
    cgroups:
    - path: "$0"
    capacity_cores: 4
- name: mem_utilization
  cgroup_memory:
    cgroups:
    - path: "$0"
    capacity_bytes: 2000
)EOF",
                              pod_.directory()));

  pod_.setCpuUsage(2000000);
  pod_.setMemory(500, 0);
  refresh();
  EXPECT_EQ("TEXT cpu_utilization=0.5,mem_utilization=0.25", reportHeader());
}

TEST_F(OrcaReporterTest, NoLimitOmitsMetric) {
  initialize(cpuAndMemoryConfig(pod_.directory()));
  pod_.setCpuUsage(1000000);
  pod_.setMemory(500, 0);
  refresh();
  EXPECT_FALSE(encode().has(TextHeader));

  pod_.setMemoryMax("1000");
  refresh();
  EXPECT_EQ("TEXT mem_utilization=0.5", reportHeader());
}

TEST_F(OrcaReporterTest, SumsCgroups) {
  pod_.setCpuMax("100000 100000");
  pod_.setMemoryMax("1000");
  app_.setCpuMax("300000 100000");
  app_.setMemoryMax("3000");
  initialize(absl::Substitute(R"EOF(
metrics:
- name: cpu_utilization
  cgroup_cpu:
    cgroups:
    - path: "$0"
    - path: "$1"
- name: mem_utilization
  cgroup_memory:
    cgroups:
    - path: "$0"
    - path: "$1"
)EOF",
                              pod_.directory(), app_.directory()));

  // 1s + 2s of CPU time in 1s on 1 + 3 cores.
  pod_.setCpuUsage(1000000);
  app_.setCpuUsage(2000000);
  pod_.setMemory(1000, 0);
  app_.setMemory(1000, 0);
  refresh();
  EXPECT_EQ("TEXT cpu_utilization=0.75,mem_utilization=0.5", reportHeader());

  // Any cgroup without a limit makes the summed limit unknown.
  app_.setCpuMax("max 100000");
  app_.setMemoryMax("max");
  refresh();
  EXPECT_FALSE(encode().has(TextHeader));
}

TEST_F(OrcaReporterTest, CpuUsageDecreaseKeepsPreviousValue) {
  pod_.setCpuMax("100000 100000");
  initialize(cpuAndMemoryConfig(pod_.directory()));

  pod_.setCpuUsage(5000000);
  refresh();
  pod_.setCpuUsage(5500000);
  refresh();
  EXPECT_EQ("TEXT cpu_utilization=0.5", reportHeader());

  // The cgroup was recreated.
  pod_.setCpuUsage(100000);
  refresh();
  EXPECT_EQ("TEXT cpu_utilization=0.5", reportHeader());

  // The next sample is computed from the new baseline.
  pod_.setCpuUsage(350000);
  refresh();
  EXPECT_EQ("TEXT cpu_utilization=0.25", reportHeader());
}

TEST_F(OrcaReporterTest, CpuReadFailureRestartsBaseline) {
  pod_.setCpuMax("100000 100000");
  initialize(cpuAndMemoryConfig(pod_.directory()));

  pod_.setCpuUsage(500000);
  refresh();
  EXPECT_EQ("TEXT cpu_utilization=0.5", reportHeader());

  pod_.removeFile("cpu.stat");
  refresh();
  EXPECT_FALSE(encode().has(TextHeader));

  // The first sample after the failure only sets the baseline.
  pod_.setCpuUsage(1000000);
  refresh();
  EXPECT_FALSE(encode().has(TextHeader));

  pod_.setCpuUsage(1250000);
  refresh();
  EXPECT_EQ("TEXT cpu_utilization=0.25", reportHeader());
}

TEST_F(OrcaReporterTest, CpuLimitReadFailureOmitsMetric) {
  pod_.setCpuMax("100000 100000");
  initialize(cpuAndMemoryConfig(pod_.directory()));

  pod_.removeFile("cpu.max");
  pod_.setCpuUsage(500000);
  refresh();
  EXPECT_FALSE(encode().has(TextHeader));

  // The baseline was kept.
  pod_.setCpuMax("100000 100000");
  pod_.setCpuUsage(1000000);
  refresh();
  EXPECT_EQ("TEXT cpu_utilization=0.5", reportHeader());
}

TEST_F(OrcaReporterTest, CpuNoElapsedTimeKeepsPreviousValue) {
  pod_.setCpuMax("100000 100000");
  initialize(cpuAndMemoryConfig(pod_.directory()));

  pod_.setCpuUsage(500000);
  refresh();
  pod_.setCpuUsage(600000);
  refresh(std::chrono::milliseconds(0));
  EXPECT_EQ("TEXT cpu_utilization=0.5", reportHeader());
  // The usage during the zero interval is counted in the next one.
  pod_.setCpuUsage(1100000);
  refresh();
  EXPECT_EQ("TEXT cpu_utilization=0.6", reportHeader());
}

TEST_F(OrcaReporterTest, MemoryReadFailureOmitsMetric) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()));
  EXPECT_EQ("TEXT mem_utilization=0.5", reportHeader());

  pod_.removeFile("memory.current");
  refresh();
  EXPECT_FALSE(encode().has(TextHeader));

  pod_.setMemory(500, 0);
  pod_.removeFile("memory.max");
  refresh();
  EXPECT_FALSE(encode().has(TextHeader));
}

TEST_F(OrcaReporterTest, NamedMetricsAndUtilization) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(absl::Substitute(R"EOF(
metrics:
- name: application_utilization
  cgroup_memory:
    cgroups:
    - path: "$0"
- name: named_metrics.pod_memory
  cgroup_memory:
    cgroups:
    - path: "$0"
- name: utilization.pod_memory
  cgroup_memory:
    cgroups:
    - path: "$0"
)EOF",
                              pod_.directory()));

  const xds::data::orca::v3::OrcaLoadReport report = parsedReport();
  EXPECT_DOUBLE_EQ(0.5, report.application_utilization());
  EXPECT_DOUBLE_EQ(0.5, report.named_metrics().at("pod_memory"));
  EXPECT_DOUBLE_EQ(0.5, report.utilization().at("pod_memory"));
}

#ifdef ENVOY_ENABLE_YAML
TEST_F(OrcaReporterTest, JsonFormat) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()) + "format: JSON\n");

  EXPECT_THAT(reportHeader(), testing::StartsWith("JSON {"));
  EXPECT_DOUBLE_EQ(0.5, parsedReport().mem_utilization());
}
#endif

TEST_F(OrcaReporterTest, BinaryFormat) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()) + "format: BINARY\n");

  const Http::TestResponseHeaderMapImpl headers = encode();
  EXPECT_FALSE(headers.has(TextHeader));
  xds::data::orca::v3::OrcaLoadReport report;
  ASSERT_TRUE(report.ParseFromString(Base64::decode(headers.get_(BinaryHeader))));
  EXPECT_DOUBLE_EQ(0.5, report.mem_utilization());
  EXPECT_DOUBLE_EQ(0.5, parsedReport().mem_utilization());
}

TEST_F(OrcaReporterTest, ExistingReportIsKept) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()));

  Http::TestResponseHeaderMapImpl text =
      encode({{":status", "200"}, {"endpoint-load-metrics", "TEXT cpu_utilization=0.9"}});
  EXPECT_EQ("TEXT cpu_utilization=0.9", text.get_(TextHeader));

  Http::TestResponseHeaderMapImpl binary =
      encode({{":status", "200"}, {"endpoint-load-metrics-bin", "abc"}});
  EXPECT_EQ("abc", binary.get_(BinaryHeader));
  EXPECT_FALSE(binary.has(TextHeader));
}

TEST_F(OrcaReporterTest, ExistingReportIsOverwritten) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()) + "overwrite_existing_report: true\n");

  Http::TestResponseHeaderMapImpl headers =
      encode({{":status", "200"},
              {"endpoint-load-metrics", "TEXT cpu_utilization=0.9"},
              {"endpoint-load-metrics-bin", "abc"}});
  EXPECT_EQ("TEXT mem_utilization=0.5", headers.get_(TextHeader));
  EXPECT_FALSE(headers.has(BinaryHeader));
}

TEST_F(OrcaReporterTest, ExistingReportIsKeptWhenNoMetricIsAvailable) {
  initialize(cpuAndMemoryConfig(pod_.directory()) + "overwrite_existing_report: true\n");

  Http::TestResponseHeaderMapImpl headers =
      encode({{":status", "200"}, {"endpoint-load-metrics", "TEXT cpu_utilization=0.9"}});
  EXPECT_EQ("TEXT cpu_utilization=0.9", headers.get_(TextHeader));
}

TEST_F(OrcaReporterTest, RefreshInterval) {
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  testing::InSequence s;
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(100), _));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(250), _)).Times(2);
  ProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(
      cpuAndMemoryConfig(pod_.directory()) + "refresh_interval: 0.25s\n", proto_config);
  LoadReporterSharedPtr reporter =
      LoadReporter::create(proto_config, api_->fileSystem(), regex_engine_, time_system_,
                           dispatcher_, tls_, processEnvLookup)
          .value();
  timer_->invokeCallback();
  timer_->invokeCallback();
}

TEST_F(OrcaReporterTest, DefaultRefreshInterval) {
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  testing::InSequence s;
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(100), _));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1000), _));
  absl::StatusOr<OrcaReporterConfigSharedPtr> config = create(cpuAndMemoryConfig(pod_.directory()));
  ASSERT_TRUE(config.ok());
  timer_->invokeCallback();
}

TEST_F(OrcaReporterTest, PathTemplateWithRewrite) {
  FakeCgroup pod{"kubepods-burstable-pod1a2b_3c4d.slice"};
  pod.setMemory(500, 0);
  pod.setMemoryMax("1000");
  env_["POD_UID"] = "1a2b-3c4d";
  initialize(absl::Substitute(R"EOF(
metrics:
- name: mem_utilization
  cgroup_memory:
    cgroups:
    - path: "$0/kubepods-burstable-pod{POD_UID}.slice"
      value_rewrites:
      - pattern: { regex: "-" }
        substitution: "_"
)EOF",
                              TestEnvironment::temporaryPath("orca_reporter")));

  EXPECT_EQ("TEXT mem_utilization=0.5", reportHeader());
}

TEST_F(OrcaReporterTest, MissingDirectory) {
  EXPECT_THAT(create(cpuAndMemoryConfig(pod_.directory() + "/missing")),
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("does not exist")));
}

TEST_F(OrcaReporterTest, MissingEnvironmentVariable) {
  EXPECT_THAT(create(cpuAndMemoryConfig(pod_.directory() + "/{POD_UID}")),
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("POD_UID")));
}

TEST_F(OrcaReporterTest, InvalidRewrite) {
  EXPECT_FALSE(create(absl::Substitute(R"EOF(
metrics:
- name: mem_utilization
  cgroup_memory:
    cgroups:
    - path: "$0"
      value_rewrites:
      - pattern: { regex: "(" }
        substitution: "_"
)EOF",
                                       pod_.directory()))
                   .ok());
}

TEST_F(OrcaReporterTest, DuplicateMetricName) {
  EXPECT_THAT(create(absl::Substitute(R"EOF(
metrics:
- name: mem_utilization
  cgroup_memory:
    cgroups:
    - path: "$0"
- name: mem_utilization
  cgroup_memory:
    cgroups:
    - path: "$0"
)EOF",
                                      pod_.directory())),
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("duplicate")));
}

TEST_F(OrcaReporterTest, UnsupportedMetricName) {
  EXPECT_THAT(create(absl::Substitute(R"EOF(
metrics:
- name: eps
  cgroup_memory:
    cgroups:
    - path: "$0"
)EOF",
                                      pod_.directory())),
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("unsupported")));
}

TEST(ParseMetricNameTest, SupportedNames) {
  using Kind = MetricTarget::Kind;
  const std::vector<std::tuple<std::string, Kind, std::string>> cases = {
      {"cpu_utilization", Kind::CpuUtilization, ""},
      {"mem_utilization", Kind::MemUtilization, ""},
      {"application_utilization", Kind::ApplicationUtilization, ""},
      {"named_metrics.foo", Kind::NamedMetric, "foo"},
      {"utilization.foo.bar", Kind::Utilization, "foo.bar"},
  };
  for (const auto& [name, kind, key] : cases) {
    absl::StatusOr<MetricTarget> target = parseMetricName(name);
    ASSERT_TRUE(target.ok()) << name;
    EXPECT_EQ(kind, target->kind) << name;
    EXPECT_EQ(key, target->key) << name;
  }
}

TEST(ParseMetricNameTest, UnsupportedNames) {
  for (const std::string name :
       {"", "eps", "rps_fractional", "named_metrics.", "utilization.", "foo", "CPU_UTILIZATION"}) {
    EXPECT_FALSE(parseMetricName(name).ok()) << name;
  }
}

TEST(OrcaReporterProtoTest, Validation) {
  ProtoConfig proto_config;
  EXPECT_THROW(TestUtility::validate(proto_config), EnvoyException);

  TestUtility::loadFromYaml(cpuAndMemoryConfig("/sys/fs/cgroup"), proto_config);
  TestUtility::validate(proto_config);

  proto_config.mutable_refresh_interval()->set_nanos(50000000);
  EXPECT_THROW(TestUtility::validate(proto_config), EnvoyException);
  proto_config.clear_refresh_interval();

  proto_config.mutable_metrics(0)->mutable_cgroup_cpu()->mutable_capacity_cores()->set_value(0);
  EXPECT_THROW(TestUtility::validate(proto_config), EnvoyException);
}

TEST(ParseMetricNameTest, InvalidKeys) {
  for (const std::string name : {"named_metrics.a=b", "utilization.x,y", "named_metrics.a b",
                                 "utilization.a:b", "named_metrics.a\"b"}) {
    EXPECT_THAT(parseMetricName(name),
                HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("invalid")))
        << name;
  }
  EXPECT_TRUE(parseMetricName("named_metrics.Pod-1.mem_2").ok());
}

TEST_F(OrcaReporterTest, InvalidMetricKeyRejected) {
  EXPECT_THAT(create(absl::Substitute(R"EOF(
metrics:
- name: "named_metrics.a=b"
  cgroup_memory:
    cgroups:
    - path: "$0"
)EOF",
                                      pod_.directory())),
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("invalid")));
}

TEST_F(OrcaReporterTest, ReportInHeadersAndTrailers) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()));

  auto [headers, trailers] = encodeWithTrailers({{":status", "200"}}, {{"foo", "bar"}});
  EXPECT_EQ("TEXT mem_utilization=0.5", headers.get_(TextHeader));
  EXPECT_EQ("TEXT mem_utilization=0.5", trailers.get_(TextHeader));
}

TEST_F(OrcaReporterTest, GrpcReportInHeadersAndTrailers) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()));

  auto [headers, trailers] = encodeWithTrailers(
      {{":status", "200"}, {"content-type", "application/grpc"}}, {{"grpc-status", "0"}});
  EXPECT_EQ("TEXT mem_utilization=0.5", headers.get_(TextHeader));
  EXPECT_EQ("TEXT mem_utilization=0.5", trailers.get_(TextHeader));
}

TEST_F(OrcaReporterTest, TrailerReportIsKept) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()));

  auto [headers, trailers] =
      encodeWithTrailers({{":status", "200"}}, {{"endpoint-load-metrics-bin", "abc"}});
  EXPECT_EQ("TEXT mem_utilization=0.5", headers.get_(TextHeader));
  EXPECT_EQ("abc", trailers.get_(BinaryHeader));
  EXPECT_FALSE(trailers.has(TextHeader));
}

TEST_F(OrcaReporterTest, HeaderAndTrailerReportsAreOverwritten) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()) + "overwrite_existing_report: true\n");

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_headers.addCopy(TextHeader, "TEXT cpu_utilization=0.9");
  auto [headers, trailers] =
      encodeWithTrailers(response_headers, {{"endpoint-load-metrics", "TEXT cpu_utilization=0.8"},
                                            {"endpoint-load-metrics-bin", "abc"}});
  EXPECT_EQ("TEXT mem_utilization=0.5", headers.get_(TextHeader));
  EXPECT_EQ("TEXT mem_utilization=0.5", trailers.get_(TextHeader));
  EXPECT_FALSE(trailers.has(BinaryHeader));
}

TEST_F(OrcaReporterTest, NoReportAvailableLeavesHeadersAndTrailers) {
  initialize(cpuAndMemoryConfig(pod_.directory()));

  auto [headers, trailers] = encodeWithTrailers({{":status", "200"}}, {{"foo", "bar"}});
  EXPECT_FALSE(headers.has(TextHeader));
  EXPECT_FALSE(trailers.has(TextHeader));
}

TEST_F(OrcaReporterTest, SameSamplingConfigSharesReporter) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  const std::string yaml = cpuAndMemoryConfig(pod_.directory());
  OrcaReporterConfigSharedPtr first = create(yaml).value();
  // overwrite_existing_report doesn't affect sampling.
  OrcaReporterConfigSharedPtr second = create(yaml + "overwrite_existing_report: true\n").value();
  EXPECT_EQ(&first->reporter(), &second->reporter());
  EXPECT_TRUE(second->overwriteExistingReport());

  OrcaReporterConfigSharedPtr other = create(yaml + "refresh_interval: 2s\n").value();
  EXPECT_NE(&first->reporter(), &other->reporter());
}

TEST_F(OrcaReporterTest, ReporterKeepsCpuBaselineAcrossConfigs) {
  pod_.setCpuMax("100000 100000");
  initialize(cpuAndMemoryConfig(pod_.directory()));
  pod_.setCpuUsage(500000);
  refresh();

  // A reload with the same config gets the running reporter.
  config_ = create(cpuAndMemoryConfig(pod_.directory())).value();
  EXPECT_EQ("TEXT cpu_utilization=0.5", reportHeader());
}

TEST_F(OrcaReporterTest, ExpiredReporterIsRecreated) {
  const std::string yaml = cpuAndMemoryConfig(pod_.directory());
  create(yaml).value().reset();

  // A new reporter creates a new timer.
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_TRUE(create(yaml).ok());
}

TEST_F(OrcaReporterTest, ReporterReleasedOnWorkerIsDeletedOnMainThread) {
  initialize(cpuAndMemoryConfig(pod_.directory()));

  Event::PostCb delete_cb;
  EXPECT_CALL(dispatcher_, isThreadSafe()).WillOnce(testing::Return(false));
  EXPECT_CALL(dispatcher_, post(_)).WillOnce([&](Event::PostCb cb) { delete_cb = std::move(cb); });
  config_.reset();
  ASSERT_TRUE(delete_cb);
  delete_cb();
}

TEST_F(OrcaReporterTest, ReportAvailableOnlyAtTrailers) {
  pod_.setCpuMax("200000 100000");
  initialize(cpuAndMemoryConfig(pod_.directory()));

  OrcaReporterFilter filter(config_);
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.encodeHeaders(headers, false));
  EXPECT_FALSE(headers.has(TextHeader));

  // The first CPU sample completes while the stream is open.
  pod_.setCpuUsage(1000000);
  refresh();
  Http::TestResponseTrailerMapImpl trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter.encodeTrailers(trailers));
  EXPECT_EQ("TEXT cpu_utilization=0.5", trailers.get_(TextHeader));
}

TEST_F(OrcaReporterTest, ReportsRequestRate) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()));

  for (int i = 0; i < 6; ++i) {
    OrcaReporterFilter filter(config_);
    Http::TestRequestHeaderMapImpl request_headers;
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.decodeHeaders(request_headers, true));
  }
  EXPECT_EQ("TEXT mem_utilization=0.5", reportHeader());

  refresh(std::chrono::seconds(2));
  EXPECT_EQ("TEXT mem_utilization=0.5,rps_fractional=3", reportHeader());
  EXPECT_DOUBLE_EQ(3, parsedReport().rps_fractional());

  // No requests in the next interval.
  refresh();
  EXPECT_EQ("TEXT mem_utilization=0.5", reportHeader());
}

TEST_F(OrcaReporterTest, RatesKeptWithoutElapsedTime) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()));

  config_->reporter().onRequest();
  refresh();
  EXPECT_EQ("TEXT mem_utilization=0.5,rps_fractional=1", reportHeader());
  config_->reporter().onRequest();
  refresh(std::chrono::milliseconds(0));
  EXPECT_EQ("TEXT mem_utilization=0.5,rps_fractional=1", reportHeader());
  refresh();
  EXPECT_EQ("TEXT mem_utilization=0.5,rps_fractional=1", reportHeader());
}

TEST_F(OrcaReporterTest, RatesNotReportedWithoutUtilization) {
  initialize(cpuAndMemoryConfig(pod_.directory()));

  config_->reporter().onRequest();
  refresh();
  EXPECT_FALSE(encode().has(TextHeader));
}

TEST_F(OrcaReporterTest, BinaryReportUpdatesClientSideWeight) {
  pod_.setCpuMax("200000 100000");
  initialize(cpuAndMemoryConfig(pod_.directory()) + "format: BINARY\n");

  for (int i = 0; i < 10; ++i) {
    config_->reporter().onRequest();
  }
  pod_.setCpuUsage(1000000);
  refresh();
  const xds::data::orca::v3::OrcaLoadReport report = parsedReport();
  EXPECT_DOUBLE_EQ(0.5, report.cpu_utilization());
  EXPECT_DOUBLE_EQ(10, report.rps_fractional());
  // 10 rps at 0.5 utilization.
  const absl::StatusOr<uint32_t> weight =
      LoadBalancingPolicies::Common::OrcaLoadReportHandler::calculateWeightFromOrcaReport(report,
                                                                                          {}, 1.0);
  ASSERT_TRUE(weight.ok()) << weight.status();
  EXPECT_EQ(20, weight.value());
}

TEST_F(OrcaReporterTest, TrailerReportKeptWhenReportBecomesUnavailable) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()) + "overwrite_existing_report: true\n");

  OrcaReporterFilter filter(config_);
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}};
  headers.addCopy(TextHeader, "TEXT application_utilization=0.9");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.encodeHeaders(headers, false));
  EXPECT_EQ("TEXT mem_utilization=0.5", headers.get_(TextHeader));

  // Memory can't be read on the next refresh, so no report is available at the trailers.
  pod_.removeFile("memory.current");
  refresh();
  Http::TestResponseTrailerMapImpl trailers{{"endpoint-load-metrics", "TEXT cpu_utilization=0.8"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter.encodeTrailers(trailers));
  EXPECT_EQ("TEXT cpu_utilization=0.8", trailers.get_(TextHeader));
}

TEST_F(OrcaReporterTest, LocalReplyNotCountedAsRequest) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()));

  config_->reporter().onRequest();
  // Local replies from earlier filters skip decodeHeaders().
  encode({{":status", "503"}});
  refresh();
  EXPECT_EQ("TEXT mem_utilization=0.5,rps_fractional=1", reportHeader());
}

TEST_F(OrcaReporterTest, NonFiniteUtilizationOmitted) {
  initialize(absl::Substitute(R"EOF(
metrics:
- name: cpu_utilization
  cgroup_cpu:
    cgroups:
    - path: "$0"
    capacity_cores: 1e-320
)EOF",
                              pod_.directory()));

  pod_.setCpuUsage(500000);
  refresh();
  EXPECT_FALSE(encode().has(TextHeader));
}

TEST_F(OrcaReporterTest, InfiniteCpuCapacityRejected) {
  absl::StatusOr<OrcaReporterConfigSharedPtr> config = create(absl::Substitute(R"EOF(
metrics:
- name: cpu_utilization
  cgroup_cpu:
    cgroups:
    - path: "$0"
    capacity_cores: "Infinity"
)EOF",
                                                                                pod_.directory()));
  EXPECT_EQ("capacity_cores must be finite", config.status().message());
}

TEST_F(OrcaReporterTest, NoRefreshAfterThreadingShutdown) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()));

  tls_.shutdownGlobalThreading();
  pod_.setMemory(800, 0);
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  refresh();
  EXPECT_EQ("TEXT mem_utilization=0.5", reportHeader());
}

TEST_F(OrcaReporterTest, NoReportOnUnregisteredThread) {
  pod_.setMemory(500, 0);
  pod_.setMemoryMax("1000");
  initialize(cpuAndMemoryConfig(pod_.directory()));

  tls_.registered_ = false;
  EXPECT_FALSE(encode().has(TextHeader));
}

} // namespace
} // namespace OrcaReporter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
