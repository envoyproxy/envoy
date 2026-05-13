#include "envoy/stats/sink.h"
#include "envoy/type/v3/scope.pb.h"

#include "source/common/config/decoded_resource_impl.h"
#include "source/common/stats/allocator_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/extensions/access_loggers/stats/config.h"
#include "source/extensions/access_loggers/stats/stats.h"

#include "test/common/memory/memory_test_utility.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/hash/hash_testing.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

class MockScopeWithGauge : public Stats::MockScope {
public:
  using Stats::MockScope::MockScope;

  MOCK_METHOD(Stats::Gauge&, gaugeFromStatNameWithTags,
              (const Stats::StatName& name, Stats::StatNameTagVectorOptConstRef tags,
               Stats::Gauge::ImportMode import_mode),
              (override));
  MOCK_METHOD(Stats::Histogram&, histogramFromStatNameWithTags,
              (const Stats::StatName& name, Stats::StatNameTagVectorOptConstRef tags,
               Stats::Histogram::Unit unit),
              (override));
};

// MockGaugeWithTags is introduced to support iterateTagStatNames which is used in
// AccessLogState destructor to reconstruct the gauge with tags.
//
// It uses StatNameDynamicStorage to own the storage for tag names and values.
// This is necessary because the tags passed to gaugeFromStatNameWithTags during
// logging are often backed by temporary storage (stack-allocated in emitLogConst)
// which is destroyed after the log call returns. By making a copy into
// tags_storage_, we ensure that iterateTagStatNames returns valid StatNames even
// if called later (e.g. in AccessLogState::~AccessLogState).
class MockGaugeWithTags : public Stats::MockGauge {
public:
  using Stats::MockGauge::MockGauge;

  void iterateTagStatNames(const TagStatNameIterFn& fn) const override {
    for (const auto& tag : tags_storage_) {
      if (!fn(tag.first->statName(), tag.second->statName()))
        return;
    }
  }

  void setTags(const Stats::StatNameTagVector& tags, Stats::SymbolTable& symbol_table) {
    tags_storage_.clear();
    tags_storage_.reserve(tags.size());
    for (const auto& tag : tags) {
      tags_storage_.emplace_back(std::make_unique<Stats::StatNameDynamicStorage>(
                                     symbol_table.toString(tag.first), symbol_table),
                                 std::make_unique<Stats::StatNameDynamicStorage>(
                                     symbol_table.toString(tag.second), symbol_table));
    }
  }

  std::vector<std::pair<std::unique_ptr<Stats::StatNameDynamicStorage>,
                        std::unique_ptr<Stats::StatNameDynamicStorage>>>
      tags_storage_;
};

class StatsAccessLoggerTest : public testing::Test {
public:
  void TearDown() override { logger_.reset(); }

  void initialize(std::string config_yaml = {}) {
    const std::string default_config_yaml = R"EOF(
      stats_scope:
        prefix: test_stat_prefix
      counters:
        - stat:
            name: counter
            tags:
              - name: mytag
                value_format: '%UPSTREAM_CLUSTER%'
          value_format: '%BYTES_SENT%'
      histograms:
        - stat:
            name: histogram
            tags:
              - name: tag
                value_format: '%UPSTREAM_TRANSPORT_FAILURE_REASON%'
          value_format: 'BYTES_RECEIVED'

)EOF";

    if (config_yaml.empty()) {
      config_yaml = default_config_yaml;
    }
    envoy::extensions::access_loggers::stats::v3::Config config;
    TestUtility::loadFromYaml(config_yaml, config);
    initialize(config);
  }

  void initialize(const envoy::extensions::access_loggers::stats::v3::Config& config) {
    auto* gauge = new NiceMock<MockGaugeWithTags>();
    gauge_ = gauge;
    // Arbitrary non-zero value to represent an active gauge.
    ON_CALL(*gauge_, value()).WillByDefault(testing::Return(10));
    // Prevent eviction.
    ON_CALL(*gauge_, used()).WillByDefault(testing::Return(true));
    gauge_ptr_ = Stats::GaugeSharedPtr(gauge_);
    gauge_->name_ = "gauge";
    gauge_->setTagExtractedName("gauge");
    ON_CALL(store_, gauge(_, _)).WillByDefault(testing::ReturnRef(*gauge_));

    ON_CALL(context_, statsScope()).WillByDefault(testing::ReturnRef(store_.mockScope()));
    ON_CALL(context_, scope()).WillByDefault(testing::ReturnRef(store_.mockScope()));
    ON_CALL(context_.server_context_, serverScope())
        .WillByDefault(testing::ReturnRef(store_.mockScope()));
    ON_CALL(context_, serverScope()).WillByDefault(testing::ReturnRef(store_.mockScope()));

    EXPECT_CALL(store_.mockScope(), createScope_(_))
        .WillRepeatedly(Invoke([this](const std::string& name) {
          auto scope_name_storage =
              std::make_unique<Stats::StatNameDynamicStorage>(name, context_.store_.symbolTable());
          auto scope = std::make_shared<NiceMock<MockScopeWithGauge>>(
              scope_name_storage->statName(), store_);
          ON_CALL(*scope, gaugeFromStatNameWithTags(_, _, _))
              .WillByDefault(
                  Invoke([this](const Stats::StatName& name, Stats::StatNameTagVectorOptConstRef,
                                Stats::Gauge::ImportMode import_mode) -> Stats::Gauge& {
                    return this->store_.gauge(this->context_.store_.symbolTable().toString(name),
                                              import_mode);
                  }));
          ON_CALL(*scope, counterFromStatNameWithTags(_, _))
              .WillByDefault(Invoke([this](const Stats::StatName& name,
                                           Stats::StatNameTagVectorOptConstRef) -> Stats::Counter& {
                return this->store_.counter(this->context_.store_.symbolTable().toString(name));
              }));

          ON_CALL(*scope, histogramFromStatNameWithTags(_, _, _))
              .WillByDefault(Invoke([scope_ptr = scope.get()](
                                        const Stats::StatName& name,
                                        Stats::StatNameTagVectorOptConstRef tags,
                                        Stats::Histogram::Unit unit) -> Stats::Histogram& {
                return scope_ptr->Stats::MockScope::histogramFromStatNameWithTags(name, tags, unit);
              }));

          scope_ = scope;
          name_storages_.push_back(std::move(scope_name_storage));
          return scope;
        }));

    logger_ = std::make_shared<StatsAccessLog>(config, context_, std::move(filter_),
                                               std::vector<Formatter::CommandParserPtr>{});
  }

  AccessLog::FilterPtr filter_;
  NiceMock<Stats::MockStore> store_;
  NiceMock<Server::Configuration::MockGenericFactoryContext> context_;
  std::vector<std::unique_ptr<Stats::StatNameDynamicStorage>> name_storages_;
  std::shared_ptr<Stats::MockScope> scope_;
  std::unique_ptr<Stats::StatNameDynamicStorage> scope_name_storage_;
  std::shared_ptr<StatsAccessLog> logger_;
  Formatter::Context formatter_context_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Stats::GaugeSharedPtr gauge_ptr_;
  Stats::MockGauge* gauge_;
};

TEST_F(StatsAccessLoggerTest, IncorrectValueFormatter) {
  const std::string cfg = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    counters:
      - stat:
          name: counter
        value_format: '%BYTES_RECEIVED%_%BYTES_SENT%'
)EOF";

  EXPECT_THROW_WITH_MESSAGE(
      initialize(cfg), EnvoyException,
      "Stats logger `value_format` string must contain exactly one substitution");
}

TEST(StatsAccessLogConfigTest, ValidationFailBothEmpty) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  envoy::extensions::access_loggers::stats::v3::Config config;
  EXPECT_THROW_WITH_MESSAGE(
      AccessLogFactory().createAccessLogInstance(config, nullptr, context), EnvoyException,
      "Either 'stat_prefix' or 'stats_scope' must be configured, but not both.");
}

TEST(StatsAccessLogConfigTest, DEPRECATED_FEATURE_TEST(ValidationFailBothSet)) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.features.enable_all_deprecated_features", "true"}});
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  envoy::extensions::access_loggers::stats::v3::Config config;
  config.set_stat_prefix("prefix");
  config.mutable_stats_scope()->set_sharing_name("scope1");
  EXPECT_THROW_WITH_MESSAGE(
      AccessLogFactory().createAccessLogInstance(config, nullptr, context), EnvoyException,
      "Either 'stat_prefix' or 'stats_scope' must be configured, but not both.");
}

TEST_F(StatsAccessLoggerTest, HistogramUnits) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    histograms:
      - stat:
          name: Unspecified
        unit: Unspecified
        value_format: '%BYTES_RECEIVED%'
      - stat:
          name: Bytes
        unit: Bytes
        value_format: '%BYTES_RECEIVED%'
      - stat:
          name: Microseconds
        unit: Microseconds
        value_format: '%BYTES_RECEIVED%'
      - stat:
          name: Milliseconds
        unit: Milliseconds
        value_format: '%BYTES_RECEIVED%'
      - stat:
          name: Percent
        unit: Percent
        value_format: '%BYTES_RECEIVED%'
)EOF";
  initialize(yaml);

  EXPECT_CALL(store_, histogram("Unspecified", Stats::Histogram::Unit::Unspecified));
  EXPECT_CALL(store_, histogram("Bytes", Stats::Histogram::Unit::Bytes));
  EXPECT_CALL(store_, histogram("Microseconds", Stats::Histogram::Unit::Microseconds));
  EXPECT_CALL(store_, histogram("Milliseconds", Stats::Histogram::Unit::Milliseconds));
  EXPECT_CALL(store_, histogram("Percent", Stats::Histogram::Unit::Percent));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, HistogramUnitsInvalid) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    histograms:
      - stat:
          name: histogram
        value_format: '%BYTES_RECEIVED%'
)EOF";
  envoy::extensions::access_loggers::stats::v3::Config config;
  TestUtility::loadFromYaml(yaml, config);
  config.mutable_histograms(0)->set_unit(
      envoy::extensions::access_loggers::stats::v3::
          Config_Histogram_Unit_Config_Histogram_Unit_INT_MAX_SENTINEL_DO_NOT_USE_);
  EXPECT_THROW_WITH_MESSAGE(initialize(config), EnvoyException,
                            "Unknown histogram unit value in stats logger: 2147483647");
}

TEST_F(StatsAccessLoggerTest, CounterBothFormatAndFixed) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    counters:
      - stat:
          name: counter
        value_format: '%BYTES_RECEIVED%'
        value_fixed: 1
)EOF";

  EXPECT_THROW_WITH_MESSAGE(
      initialize(yaml), EnvoyException,
      "Stats logger cannot have both `value_format` and `value_fixed` configured.");
}

TEST_F(StatsAccessLoggerTest, CounterNoValueConfig) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    counters:
      - stat:
          name: counter
)EOF";

  EXPECT_THROW_WITH_MESSAGE(
      initialize(yaml), EnvoyException,
      "Stats logger counter must have either `value_format` or `value_fixed`.");
}

// Format string resolved to empty optional (no value available).
TEST_F(StatsAccessLoggerTest, NoValueFormatted) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    counters:
      - stat:
          name: counter
        value_format: '%RESPONSE_CODE_DETAILS%'
)EOF";

  initialize(yaml);

  absl::optional<std::string> nullopt{absl::nullopt};
  EXPECT_CALL(stream_info_, responseCodeDetails()).WillRepeatedly(testing::ReturnRef(nullopt));
  EXPECT_CALL(store_, counter(_)).Times(0);
  EXPECT_LOG_CONTAINS("error", "Stats access logger computed non-number value: ", {
    logger_->log(formatter_context_, stream_info_);
  });
}

// Format string resolved to a non-number string.
TEST_F(StatsAccessLoggerTest, NonNumberValueFormatted) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    histograms:
      - stat:
          name: counter
        value_format: '%RESPONSE_CODE_DETAILS%'
)EOF";

  initialize(yaml);

  absl::optional<std::string> not_a_number{"hello"};
  EXPECT_CALL(stream_info_, responseCodeDetails()).WillRepeatedly(testing::ReturnRef(not_a_number));
  EXPECT_CALL(store_, counter(_)).Times(0);
  EXPECT_LOG_CONTAINS("error", "Stats access logger formatted a string that isn't a number: hello",
                      { logger_->log(formatter_context_, stream_info_); });
}

// Format string resolved to a number string.
TEST_F(StatsAccessLoggerTest, NumberStringValueFormatted) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    counters:
      - stat:
          name: counter
        value_format: '%RESPONSE_CODE_DETAILS%'
)EOF";

  initialize(yaml);

  absl::optional<std::string> a_number{"42"};
  EXPECT_CALL(stream_info_, responseCodeDetails()).WillRepeatedly(testing::ReturnRef(a_number));
  EXPECT_CALL(store_, counter(_));
  EXPECT_CALL(store_.counter_, add(42));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, CounterValueFixed) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    counters:
      - stat:
          name: counter
        value_fixed: 42
)EOF";

  initialize(yaml);

  absl::optional<std::string> a_number{"42"};
  EXPECT_CALL(stream_info_, responseCodeDetails()).WillRepeatedly(testing::ReturnRef(a_number));
  EXPECT_CALL(store_, counter(_));
  EXPECT_CALL(store_.counter_, add(42));
  logger_->log(formatter_context_, stream_info_);
}

// Histogram values are in the range 0-1.0, so ensure that fractional values work.
TEST_F(StatsAccessLoggerTest, HistogramPercent) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    histograms:
      - stat:
          name: histogram
        unit: Percent
        value_format: '%RESPONSE_CODE_DETAILS%'
)EOF";

  initialize(yaml);

  absl::optional<std::string> a_number{"0.1"};
  EXPECT_CALL(stream_info_, responseCodeDetails()).WillRepeatedly(testing::ReturnRef(a_number));
  EXPECT_CALL(store_, histogram(_, Stats::Histogram::Unit::Percent))
      .WillOnce(
          Invoke([&](const std::string& name, Stats::Histogram::Unit unit) -> Stats::Histogram& {
            auto* histogram = new NiceMock<Stats::MockHistogram>(); // symbol_table_);
            histogram->name_ = name;
            histogram->unit_ = unit;
            histogram->store_ = &store_;
            store_.histograms_.emplace_back(histogram);

            EXPECT_CALL(*histogram, recordValue(Stats::Histogram::PercentScale / 10));
            return *histogram;
          }));

  logger_->log(formatter_context_, stream_info_);
}

// Test that a tag formatter that doesn't have a value becomes an empty string.
TEST_F(StatsAccessLoggerTest, EmptyTagFormatter) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    counters:
      - stat:
          name: counter
          tags:
            - name: tag
              value_format: '%RESPONSE_CODE_DETAILS%:%RESPONSE_CODE%'
        value_fixed: 1
)EOF";

  initialize(yaml);

  absl::optional<std::string> nullopt{absl::nullopt};
  EXPECT_CALL(stream_info_, responseCodeDetails()).WillRepeatedly(testing::ReturnRef(nullopt));
  EXPECT_CALL(stream_info_, responseCode())
      .WillRepeatedly(testing::Return(absl::optional<uint32_t>{200}));
  EXPECT_CALL(*scope_, counterFromStatNameWithTags(_, _))
      .WillOnce(
          testing::Invoke([this](const Stats::StatName& name,
                                 Stats::StatNameTagVectorOptConstRef tags) -> Stats::Counter& {
            EXPECT_EQ("counter", scope_->symbolTable().toString(name));
            EXPECT_EQ(1, tags->get().size());
            EXPECT_EQ(":200", scope_->symbolTable().toString(tags->get().front().second));

            return store_.counter_;
          }));
  EXPECT_CALL(store_.counter_, add(1));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, GaugeNonNumberValueFormatted) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_format: '%RESPONSE_CODE_DETAILS%'
        set:
          log_type: DownstreamEnd
)EOF";

  initialize(yaml);

  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);

  absl::optional<std::string> not_a_number{"hello"};
  EXPECT_CALL(stream_info_, responseCodeDetails()).WillRepeatedly(testing::ReturnRef(not_a_number));
  EXPECT_CALL(store_, gauge(_, _)).Times(0);
  // Note: Logging is verified in NonNumberValueFormatted. We skip verification here due to shared
  // rate limiting in ENVOY_LOG_PERIODIC_MISC which causes this second test to suppress the log.
  logger_->log(formatter_context_, stream_info_);
}

// Format string resolved to a number string.
TEST_F(StatsAccessLoggerTest, GaugeNumberValueFormatted) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_format: '%BYTES_RECEIVED%'
        set:
          log_type: DownstreamEnd
)EOF";

  initialize(yaml);

  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);

  EXPECT_CALL(stream_info_, bytesReceived()).WillRepeatedly(testing::Return(42));
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::NeverImport));
  EXPECT_CALL(*gauge_, set(42));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, GaugeValueFixed) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
          tags:
            - name: mytag
              value_format: '%UPSTREAM_CLUSTER%'
            - name: other_tag
              value_format: '%RESPONSE_CODE%'
        value_fixed: 42
        set:
          log_type: DownstreamEnd
)EOF";

  initialize(yaml);

  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);

  absl::optional<std::string> a_number{"42"};
  EXPECT_CALL(stream_info_, responseCodeDetails()).WillRepeatedly(testing::ReturnRef(a_number));
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::NeverImport));
  EXPECT_CALL(*gauge_, set(42));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, GaugeOperationTypeSet) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 42
        set:
          log_type: DownstreamEnd
)EOF";
  initialize(yaml);

  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);

  absl::optional<std::string> a_number{"42"};
  EXPECT_CALL(stream_info_, responseCodeDetails()).WillRepeatedly(testing::ReturnRef(a_number));
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::NeverImport));
  EXPECT_CALL(*gauge_, set(42));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, GaugeBothFormatAndFixed) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_format: '%BYTES_RECEIVED%'
        value_fixed: 1
        set:
          log_type: DownstreamEnd
)EOF";

  EXPECT_THROW_WITH_MESSAGE(
      initialize(yaml), EnvoyException,
      "Stats logger cannot have both `value_format` and `value_fixed` configured.");
}

TEST_F(StatsAccessLoggerTest, GaugeNoValueConfig) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        set:
          log_type: DownstreamEnd
)EOF";
  EXPECT_THROW_WITH_MESSAGE(initialize(yaml), EnvoyException,
                            "Stats logger gauge must have either `value_format` or `value_fixed`.");
}

TEST_F(StatsAccessLoggerTest, GaugeBothSetAndAddSubtract) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 42
        add_subtract:
          add_log_type: DownstreamStart
          sub_log_type: DownstreamEnd
        set:
          log_type: DownstreamEnd
)EOF";
  EXPECT_THROW_WITH_MESSAGE(
      initialize(yaml), EnvoyException,
      "Stats logger gauge cannot have both SET and PAIRED_ADD/PAIRED_SUBTRACT operations.");
}

TEST_F(StatsAccessLoggerTest, GaugeMultipleAdd) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 42
        add_subtract:
          add_log_type: DownstreamStart
          sub_log_type: DownstreamStart
)EOF";
  EXPECT_THROW_WITH_MESSAGE(initialize(yaml), EnvoyException,
                            "Duplicate access log type '4' in gauge operations.");
}

TEST_F(StatsAccessLoggerTest, GaugeNeitherSetNorAddSubtract) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 42
)EOF";
  EXPECT_THROW_WITH_MESSAGE(initialize(yaml), EnvoyException,
                            "Stats logger gauge must have at least one operation configured.");
}

TEST_F(StatsAccessLoggerTest, GaugeAddSubtractBehavior) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 1
        add_subtract:
          add_log_type: DownstreamStart
          sub_log_type: DownstreamEnd
)EOF";
  initialize(yaml);

  // Case 1: AccessLogType matches neither -> no change
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::NotSet);
  EXPECT_CALL(store_, gauge(_, _)).Times(0);
  logger_->log(formatter_context_, stream_info_);
  testing::Mock::VerifyAndClearExpectations(&store_);
  testing::Mock::VerifyAndClearExpectations(&*gauge_);

  // Case 2: AccessLogType matches add_at -> add
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamStart);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate)).Times(testing::AtLeast(1));
  EXPECT_CALL(*gauge_, add(1));
  logger_->log(formatter_context_, stream_info_);
  testing::Mock::VerifyAndClearExpectations(&store_);
  testing::Mock::VerifyAndClearExpectations(&*gauge_);

  // Case 3: AccessLogType matches subtract_at after add -> subtract
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate)).Times(testing::AtLeast(1));
  EXPECT_CALL(*gauge_, sub(1));
  logger_->log(formatter_context_, stream_info_);
  testing::Mock::VerifyAndClearExpectations(&store_);
  testing::Mock::VerifyAndClearExpectations(&*gauge_);
}

TEST_F(StatsAccessLoggerTest, GaugeAddZeroValue) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 0
        add_subtract:
          add_log_type: DownstreamStart
          sub_log_type: DownstreamEnd
)EOF";
  initialize(yaml);

  // Trigger ADD with value 0
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamStart);

  // The first time it gets the gauge and calls add(0).
  // We don't expect it to actually be added to inflight_gauges_.
  EXPECT_CALL(store_, gauge(_, _)).WillRepeatedly(testing::ReturnRef(*gauge_));

  EXPECT_CALL(*gauge_, add(0)).Times(0); // addInflightGauge skips if value == 0
  logger_->log(formatter_context_, stream_info_);

  // Trigger SUBTRACT
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);
  // We expect no `sub(0)` interaction here because it wasn't added to inflight_gauges_.
  EXPECT_CALL(*gauge_, sub(_)).Times(0);
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, GaugeSubtractBeforeAdd) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 1
        add_subtract:
          add_log_type: DownstreamStart
          sub_log_type: DownstreamEnd
)EOF";
  initialize(yaml);

  // Subtract without add -> logs instead of crashing
  EXPECT_LOG_CONTAINS("error",
                      "Stats access logger gauge paired subtract was skipped due to no "
                      "corresponding add, possibly due to misconfigured events: gauge",
                      {
                        formatter_context_.setAccessLogType(
                            envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);
                        EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate))
                            .Times(testing::AtLeast(1));
                        EXPECT_CALL(*gauge_, add(_)).Times(0);
                        EXPECT_CALL(*gauge_, sub(_)).Times(0);
                        logger_->log(formatter_context_, stream_info_);
                      });
}

TEST_F(StatsAccessLoggerTest, GaugeMultipleSubAfterAdd) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 1
        add_subtract:
          add_log_type: DownstreamStart
          sub_log_type: DownstreamEnd
)EOF";
  initialize(yaml);

  // Trigger ADD
  {
    formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamStart);
    EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate)).Times(testing::AtLeast(1));
    EXPECT_CALL(*gauge_, add(1));
    logger_->log(formatter_context_, stream_info_);
  }

  // Trigger SUB (first time)
  {
    formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);
    EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate)).Times(testing::AtLeast(1));
    EXPECT_CALL(*gauge_, sub(1));
    logger_->log(formatter_context_, stream_info_);
  }

  // Trigger SUB (second time) -> throttled due to previous tests, so no log expected
  {
    formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);
    EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate)).Times(testing::AtLeast(1));
    EXPECT_CALL(*gauge_, sub(_)).Times(0);
    logger_->log(formatter_context_, stream_info_);
  }
}

TEST_F(StatsAccessLoggerTest, PairedSubtractIgnoresConfiguredValue) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 10
        add_subtract:
          add_log_type: DownstreamStart
          sub_log_type: DownstreamEnd
)EOF";
  initialize(yaml);

  // Trigger ADD with value 10
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamStart);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate)).Times(testing::AtLeast(1));
  EXPECT_CALL(*gauge_, add(10));
  logger_->log(formatter_context_, stream_info_);

  // Trigger SUBTRACT. Should still subtract 10.
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate)).Times(testing::AtLeast(1));
  EXPECT_CALL(*gauge_, sub(10));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, DestructionSubtractsRemainingValue) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 10
        add_subtract:
          add_log_type: DownstreamStart
          sub_log_type: DownstreamEnd
)EOF";
  initialize(yaml);

  // Trigger ADD using a local StreamInfo so we can control its lifetime.
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamStart);

  NiceMock<StreamInfo::MockStreamInfo> local_stream_info;

  // Called once on log().
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate)).Times(testing::AtLeast(1));
  EXPECT_CALL(*gauge_, add(10));
  logger_->log(formatter_context_, local_stream_info);

  // Expect subtraction on destruction
  EXPECT_CALL(*gauge_, sub(10));

  // Destroy logger before stream_info to simulate logger config deletion while stream is active
  logger_.reset();

  // local_stream_info goes out of scope here.
}

TEST_F(StatsAccessLoggerTest, AccessLogStateDestructorSubtractsFromSavedGauge) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
          tags:
            - name: tag_name
              value_format: '%RESPONSE_CODE%'
            - name: another_tag
              value_format: 'value_fixed'
        value_fixed: 10
        add_subtract:
          add_log_type: DownstreamStart
          sub_log_type: DownstreamEnd
)EOF";
  initialize(yaml);

  auto* mock_scope = dynamic_cast<MockScopeWithGauge*>(scope_.get());
  ASSERT_TRUE(mock_scope != nullptr);

  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamStart);

  Stats::StatName saved_name;
  std::vector<std::pair<std::string, std::string>> saved_tags_strs;

  NiceMock<StreamInfo::MockStreamInfo> local_stream_info;
  EXPECT_CALL(local_stream_info, responseCode())
      .WillRepeatedly(testing::Return(absl::optional<uint32_t>{200}));

  // Initial lookup and add
  EXPECT_CALL(*mock_scope, gaugeFromStatNameWithTags(_, _, Stats::Gauge::ImportMode::Accumulate))
      .WillRepeatedly(
          Invoke([&](const Stats::StatName& name, Stats::StatNameTagVectorOptConstRef tags,
                     Stats::Gauge::ImportMode) -> Stats::Gauge& {
            saved_name = name;
            if (tags) {
              for (const auto& tag : tags->get()) {
                saved_tags_strs.emplace_back(store_.symbolTable().toString(tag.first),
                                             store_.symbolTable().toString(tag.second));
              }
              EXPECT_FALSE(saved_tags_strs.empty());
              auto* gauge_with_tags = dynamic_cast<MockGaugeWithTags*>(gauge_);
              EXPECT_TRUE(gauge_with_tags != nullptr);
              gauge_with_tags->setTags(tags->get(), store_.symbolTable());
            }
            return *gauge_;
          }));

  EXPECT_CALL(*gauge_, add(10));
  logger_->log(formatter_context_, local_stream_info);

  // The destructor of AccessLogState should call sub(10, _) directly on the saved gauge
  // This will trigger a second lookup using gaugeFromString (tags == absl::nullopt).
  EXPECT_CALL(*gauge_, sub(10));

  // local_stream_info goes out of scope here, triggering AccessLogState destructor.
}

TEST_F(StatsAccessLoggerTest, SameGaugeAddSubtractDefinedTwice) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 10
        add_subtract:
          add_log_type: DownstreamStart
          sub_log_type: DownstreamEnd
      - stat:
          name: gauge
        value_fixed: 20
        add_subtract:
          add_log_type: TcpUpstreamConnected
          sub_log_type: DownstreamEnd
)EOF";
  initialize(yaml);

  // Trigger ADD for the first definition (DownstreamStart)
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamStart);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate)).Times(testing::AtLeast(1));
  EXPECT_CALL(*gauge_, add(10));
  logger_->log(formatter_context_, stream_info_);

  // Trigger ADD for the second definition (TcpUpstreamConnected)
  formatter_context_.setAccessLogType(
      envoy::data::accesslog::v3::AccessLogType::TcpUpstreamConnected);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate)).Times(testing::AtLeast(1));
  // The second gauge is added on TcpUpstreamConnected
  EXPECT_CALL(*gauge_, add(20));
  logger_->log(formatter_context_, stream_info_);

  // Trigger SUBTRACT for both (DownstreamEnd)
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate)).Times(2);
  EXPECT_CALL(*gauge_, sub(10));
  EXPECT_CALL(*gauge_, sub(20));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, GaugeNotSet) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 42
        set:
          log_type: NotSet
)EOF";
  EXPECT_THROW_WITH_MESSAGE(initialize(yaml), EnvoyException,
                            "Stats logger gauge set operation must have a valid log type.");
}

TEST_F(StatsAccessLoggerTest, StatsScope) {
  const std::string yaml = R"EOF(
    stats_scope:
      max_counters: 10
    counters:
      - stat:
          name: counter
        value_fixed: 1
)EOF";

  initialize(yaml);

  Formatter::Context formatter_context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  // The newly created scope for "test_scope" is stored at the end of `name_storages_` but
  // since `scope_` only stores the last created scope for non-"scope_discovery", it should
  // be exactly `scope_` here.
  EXPECT_CALL(*scope_, counterFromStatNameWithTags(_, _))
      .WillOnce(testing::ReturnRef(store_.counter_));
  EXPECT_CALL(store_.counter_, add(1));
  logger_->log(formatter_context, stream_info);
}

TEST_F(StatsAccessLoggerTest, DropStatAction) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    counters:
      - stat:
          name: counter
          tags:
            - name: foo
              value_format: bar
              rules:
                matcher_tree:
                  input:
                    name: stat_tag_value_input
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.stats.v3.StatTagValueInput
                  exact_match_map:
                    map:
                      "bar":
                        action:
                          name: generic_stat_action
                          typed_config:
                            "@type": type.googleapis.com/envoy.extensions.matching.actions.transform_stat.v3.TransformStat
                            drop_stat: {}
        value_fixed: 1
)EOF";

  initialize(yaml);

  // Case 1: Filter matches (tag foo=bar), so drop action is executed.
  EXPECT_CALL(store_, counter(_)).Times(0);
  logger_->log(formatter_context_, stream_info_);

  const std::string yaml2 = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    counters:
      - stat:
          name: counter
          tags:
            - name: foo
              value_format: baz
              rules:
                matcher_tree:
                  input:
                    name: stat_tag_value_input
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.stats.v3.StatTagValueInput
                  exact_match_map:
                    map:
                      "bar":
                        action:
                          name: generic_stat_action
                          typed_config:
                            "@type": type.googleapis.com/envoy.extensions.matching.actions.transform_stat.v3.TransformStat
                            drop_stat: {}
        value_fixed: 1
)EOF";
  initialize(yaml2);

  // Case 2: Filter does not match (tag foo=baz), so drop action is NOT executed.
  EXPECT_CALL(store_, counter(_));
  EXPECT_CALL(store_.counter_, add(1));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, DropStatActionOnHistogram) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    histograms:
      - stat:
          name: histogram
          tags:
            - name: foo
              value_format: bar
              rules:
                matcher_tree:
                  input:
                    name: stat_tag_value_input
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.stats.v3.StatTagValueInput
                  exact_match_map:
                    map:
                      "bar":
                        action:
                          name: generic_stat_action
                          typed_config:
                            "@type": type.googleapis.com/envoy.extensions.matching.actions.transform_stat.v3.TransformStat
                            drop_stat: {}
        unit: Bytes
        value_format: '%BYTES_RECEIVED%'
)EOF";

  initialize(yaml);

  // Case 1: Filter matches (tag foo=bar), so drop action is executed.
  EXPECT_CALL(store_, histogram(_, _)).Times(0);
  logger_->log(formatter_context_, stream_info_);

  const std::string yaml2 = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    histograms:
      - stat:
          name: histogram
          tags:
            - name: foo
              value_format: baz
              rules:
                matcher_tree:
                  input:
                    name: stat_tag_value_input
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.stats.v3.StatTagValueInput
                  exact_match_map:
                    map:
                      "bar":
                        action:
                          name: generic_stat_action
                          typed_config:
                            "@type": type.googleapis.com/envoy.extensions.matching.actions.transform_stat.v3.TransformStat
                            drop_stat: {}
        unit: Bytes
        value_format: '%BYTES_RECEIVED%'
)EOF";
  initialize(yaml2);

  // Case 2: Filter does not match (tag foo=baz), so drop action is NOT executed.
  Stats::MockHistogram mock_histogram;
  EXPECT_CALL(store_, histogram(_, Stats::Histogram::Unit::Bytes))
      .WillOnce(testing::ReturnRef(mock_histogram));
  EXPECT_CALL(mock_histogram, recordValue(_));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, StatTagFilterUpdateTag) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    counters:
      - stat:
          name: counter
          tags:
            - name: foo
              value_format: bar
              rules:
                matcher_tree:
                  input:
                    name: stat_tag_value_input
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.stats.v3.StatTagValueInput
                  exact_match_map:
                    map:
                      "bar":
                        action:
                          name: generic_stat_action
                          typed_config:
                            "@type": type.googleapis.com/envoy.extensions.matching.actions.transform_stat.v3.TransformStat
                            update_tag:
                              new_tag_value: baz
        value_fixed: 1
)EOF";

  initialize(yaml);

  // Case 1: Filter matches (tag foo=bar), so update tag action is executed.
  EXPECT_CALL(*scope_, counterFromStatNameWithTags(_, _))
      .WillOnce(
          testing::Invoke([this](const Stats::StatName& name,
                                 Stats::StatNameTagVectorOptConstRef tags) -> Stats::Counter& {
            EXPECT_EQ("counter", scope_->symbolTable().toString(name));
            EXPECT_EQ(1, tags->get().size());
            EXPECT_EQ("foo", scope_->symbolTable().toString(tags->get()[0].first));
            EXPECT_EQ("baz", scope_->symbolTable().toString(tags->get()[0].second));
            return scope_->counterFromStatNameWithTags_(name, tags);
          }));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, StatTagFilterDropTag) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    counters:
      - stat:
          name: counter
          tags:
            - name: foo
              value_format: bar
              rules:
                matcher_tree:
                  input:
                    name: stat_tag_value_input
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.stats.v3.StatTagValueInput
                  exact_match_map:
                    map:
                      "bar":
                        action:
                          name: generic_stat_action
                          typed_config:
                            "@type": type.googleapis.com/envoy.extensions.matching.actions.transform_stat.v3.TransformStat
                            drop_tag: {}
        value_fixed: 1
)EOF";

  initialize(yaml);

  // Case 1: Filter matches (tag foo=bar), so drop tag action is executed.
  EXPECT_CALL(*scope_, counterFromStatNameWithTags(_, _))
      .WillOnce(
          testing::Invoke([this](const Stats::StatName& name,
                                 Stats::StatNameTagVectorOptConstRef tags) -> Stats::Counter& {
            EXPECT_EQ("counter", scope_->symbolTable().toString(name));
            EXPECT_EQ(0, tags->get().size());
            return scope_->counterFromStatNameWithTags_(name, tags);
          }));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, DropStatActionOnGauge) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
          tags:
            - name: foo
              value_format: bar
              rules:
                matcher_tree:
                  input:
                    name: stat_tag_value_input
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.stats.v3.StatTagValueInput
                  exact_match_map:
                    map:
                      "bar":
                        action:
                          name: generic_stat_action
                          typed_config:
                            "@type": type.googleapis.com/envoy.extensions.matching.actions.transform_stat.v3.TransformStat
                            drop_stat: {}
        value_fixed: 1
        set:
          log_type: DownstreamEnd
)EOF";

  initialize(yaml);
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);

  // Case 1: Filter matches (tag foo=bar), so drop action is executed.
  EXPECT_CALL(store_, gauge(_, _)).Times(0);
  logger_->log(formatter_context_, stream_info_);

  const std::string yaml2 = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
          tags:
            - name: foo
              value_format: baz
              rules:
                matcher_tree:
                  input:
                    name: stat_tag_value_input
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.stats.v3.StatTagValueInput
                  exact_match_map:
                    map:
                      "bar":
                        action:
                          name: generic_stat_action
                          typed_config:
                            "@type": type.googleapis.com/envoy.extensions.matching.actions.transform_stat.v3.TransformStat
                            drop_stat: {}
        value_fixed: 1
        set:
          log_type: DownstreamEnd
)EOF";
  initialize(yaml2);
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);

  // Case 2: Filter does not match (tag foo=baz), so drop action is NOT executed.
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::NeverImport));
  EXPECT_CALL(*gauge_, set(1));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, StatTagFilterUpdateTagOnGauge) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
          tags:
            - name: foo
              value_format: bar
              rules:
                matcher_tree:
                  input:
                    name: stat_tag_value_input
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.stats.v3.StatTagValueInput
                  exact_match_map:
                    map:
                      "bar":
                        action:
                          name: generic_stat_action
                          typed_config:
                            "@type": type.googleapis.com/envoy.extensions.matching.actions.transform_stat.v3.TransformStat
                            update_tag:
                              new_tag_value: baz
        value_fixed: 1
        set:
          log_type: DownstreamEnd
)EOF";

  initialize(yaml);
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);

  auto* mock_scope = dynamic_cast<MockScopeWithGauge*>(scope_.get());
  ASSERT_TRUE(mock_scope != nullptr);

  // Case 1: Filter matches (tag foo=bar), so update tag action is executed.
  EXPECT_CALL(*mock_scope, gaugeFromStatNameWithTags(_, _, _))
      .WillOnce(Invoke([&](const Stats::StatName& name, Stats::StatNameTagVectorOptConstRef tags,
                           Stats::Gauge::ImportMode) -> Stats::Gauge& {
        EXPECT_EQ("gauge", scope_->symbolTable().toString(name));
        EXPECT_EQ(1, tags->get().size());
        EXPECT_EQ("foo", scope_->symbolTable().toString(tags->get()[0].first));
        EXPECT_EQ("baz", scope_->symbolTable().toString(tags->get()[0].second));
        return *gauge_;
      }));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, StatTagFilterUpdateTagOnHistogram) {
  const std::string yaml = R"EOF(
    stats_scope:
      prefix: test_stat_prefix
    histograms:
      - stat:
          name: histogram
          tags:
            - name: foo
              value_format: bar
              rules:
                matcher_tree:
                  input:
                    name: stat_tag_value_input
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.stats.v3.StatTagValueInput
                  exact_match_map:
                    map:
                      "bar":
                        action:
                          name: generic_stat_action
                          typed_config:
                            "@type": type.googleapis.com/envoy.extensions.matching.actions.transform_stat.v3.TransformStat
                            update_tag:
                              new_tag_value: baz
        unit: Bytes
        value_format: '%BYTES_RECEIVED%'
)EOF";

  initialize(yaml);

  auto* mock_scope = dynamic_cast<MockScopeWithGauge*>(scope_.get());
  ASSERT_TRUE(mock_scope != nullptr);

  // Case 1: Filter matches (tag foo=bar), so update tag action is executed.
  EXPECT_CALL(*mock_scope, histogramFromStatNameWithTags(_, _, _))
      .WillOnce(Invoke([&](const Stats::StatName& name, Stats::StatNameTagVectorOptConstRef tags,
                           Stats::Histogram::Unit) -> Stats::Histogram& {
        EXPECT_EQ("histogram", scope_->symbolTable().toString(name));
        EXPECT_EQ(1, tags->get().size());
        EXPECT_EQ("foo", scope_->symbolTable().toString(tags->get()[0].first));
        EXPECT_EQ("baz", scope_->symbolTable().toString(tags->get()[0].second));
        return store_.mockScope().histogramFromStatNameWithTags(name, tags,
                                                                Stats::Histogram::Unit::Bytes);
      }));
  logger_->log(formatter_context_, stream_info_);
}

TEST(GaugeKeyTest, EqualityAndHashing) {
  Stats::SymbolTableImpl symbol_table;
  Stats::StatNamePool pool(symbol_table);

  using GaugeKey = AccessLoggers::StatsAccessLog::GaugeKey;

  Stats::StatName name1 = pool.add("name1");
  Stats::StatName name2 = pool.add("name2");

  GaugeKey key1(name1, absl::nullopt);
  GaugeKey key2(name1, absl::nullopt);
  GaugeKey key3(name2, absl::nullopt);

  // Basic equality
  EXPECT_EQ(key1, key2);
  EXPECT_NE(key1, key3);

  // Hash equality
  EXPECT_EQ(absl::Hash<GaugeKey>{}(key1), absl::Hash<GaugeKey>{}(key2));
  EXPECT_NE(absl::Hash<GaugeKey>{}(key1), absl::Hash<GaugeKey>{}(key3));

  // Tags
  Stats::StatName tag_n1 = pool.add("tag_n1");
  Stats::StatName tag_v1 = pool.add("tag_v1");
  Stats::StatName tag_v2 = pool.add("tag_v2");

  Stats::StatNameTagVector tags1 = {{tag_n1, tag_v1}};
  Stats::StatNameTagVector tags2 = {{tag_n1, tag_v2}};

  GaugeKey key_tags1(name1, std::cref(tags1));
  GaugeKey key_tags2(name1, std::cref(tags1));
  GaugeKey key_tags3(name1, std::cref(tags2));

  EXPECT_EQ(key_tags1, key_tags2);
  EXPECT_NE(key_tags1, key_tags3);
  EXPECT_NE(key1, key_tags1); // No tags vs tags

  EXPECT_EQ(absl::Hash<GaugeKey>{}(key_tags1), absl::Hash<GaugeKey>{}(key_tags2));
  EXPECT_NE(absl::Hash<GaugeKey>{}(key_tags1), absl::Hash<GaugeKey>{}(key_tags3));

  // Borrowed vs Owned
  GaugeKey key_owned(name1, std::cref(tags1));
  key_owned.makeOwned();

  EXPECT_EQ(key_tags1, key_owned); // Borrowed vs Owned should be equal if content is same //
                                   // Borrowed vs Owned should be equal if content is same
}

TEST(GaugeKeyTest, VerifyAbslHashCorrectness) {
  Stats::SymbolTableImpl symbol_table;
  Stats::StatNamePool pool(symbol_table);

  using GaugeKey = AccessLoggers::StatsAccessLog::GaugeKey;

  Stats::StatName name1 = pool.add("name1");
  Stats::StatName name2 = pool.add("name2");
  Stats::StatName tag_n1 = pool.add("tag_n1");
  Stats::StatName tag_v1 = pool.add("tag_v1");
  Stats::StatName tag_v2 = pool.add("tag_v2");

  Stats::StatNameTagVector tags1 = {{tag_n1, tag_v1}};
  Stats::StatNameTagVector tags2 = {{tag_n1, tag_v2}};

  GaugeKey key_empty1(name1, absl::nullopt);
  GaugeKey key_empty2(name2, absl::nullopt);

  GaugeKey key_borrowed(name1, std::cref(tags1));
  GaugeKey key_owned(name1, std::cref(tags1));
  key_owned.makeOwned();

  GaugeKey key_tags2(name1, std::cref(tags2));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(std::move(key_empty1), std::move(key_empty2), std::move(key_borrowed),
                      std::move(key_owned), std::move(key_tags2))));
}

TEST(GaugeKeyTest, ExactMemoryFootprint) {
  Stats::SymbolTableImpl symbol_table;
  Stats::StatNamePool pool(symbol_table);

  using GaugeKey = AccessLoggers::StatsAccessLog::GaugeKey;

  // Static size check
  EXPECT_LE(sizeof(GaugeKey), 64);

  Stats::StatName name = pool.add("test_gauge");
  Stats::StatName tag_n1 = pool.add("tag_n1");
  Stats::StatName tag_v1 = pool.add("tag_v1");

  Stats::StatNameTagVector tags = {{tag_n1, tag_v1}};

  // 1. Check memory usage of empty GaugeKey (no heap should be used by GaugeKey itself).
  {
    Memory::TestUtil::MemoryTest memory_test;
    GaugeKey key(name, absl::nullopt);
    // GaugeKey on stack, no heap should be allocated.
    EXPECT_MEMORY_EQ(memory_test.consumedBytes(), 0);
  }

  // 2. Check memory usage of Borrowed tags GaugeKey.
  {
    Memory::TestUtil::MemoryTest memory_test;
    GaugeKey key(name, std::cref(tags));
    // Borrowed tags should NOT cause heap allocation by GaugeKey itself.
    EXPECT_MEMORY_EQ(memory_test.consumedBytes(), 0);
  }

  // 3. Check memory usage after making it owned.
  {
    GaugeKey key(name, std::cref(tags));

    Memory::TestUtil::MemoryTest memory_test;
    key.makeOwned();

    // We expect some non-zero heap allocation for owned tags.
    // The exact match depends on platform calibration (canonical release build).
    // We use LE to check if it's within bounds. For exactly 1 tag, it should be small.
    // Let's verify it exceeds 0 but is less than some reasonable limit (e.g., 64 bytes).
    EXPECT_MEMORY_LE(memory_test.consumedBytes(), 64);
  }
}

TEST_F(StatsAccessLoggerTest, AccessLogStateMemoryFootprint) {
  initialize();
  auto access_log_state = std::make_shared<AccessLogState>(logger_);

  // Static size check
  EXPECT_LE(sizeof(AccessLogState), 128);

  Stats::StatNamePool pool(store_.symbolTable());

  Stats::StatName tag_n = pool.add("tag_n");
  Stats::StatName tag_v = pool.add("tag_v");

  Stats::StatNameTagVector tags = {{tag_n, tag_v}};

  const int NUM_ITEMS = 10000;

  // Pre-intern names to isolate map insertion overhead from SymbolTable allocation.
  std::vector<Stats::StatName> names;
  names.reserve(NUM_ITEMS);
  for (int i = 0; i < NUM_ITEMS; ++i) {
    names.push_back(pool.add("test_gauge_" + std::to_string(i)));
  }

  // Use single MemoryTest scope to measure net difference from creation to destruction (Check for
  // absolute zero leaks).
  {
    Memory::TestUtil::MemoryTest memory_test;
    auto access_log_state = std::make_shared<AccessLogState>(logger_);

    // 1. Add multiple items
    for (int i = 0; i < NUM_ITEMS; ++i) {
      access_log_state->addInflightGauge(names[i], std::cref(tags),
                                         Stats::Gauge::ImportMode::Accumulate, 1, {});
    }

    // Verify it is within bounds (e.g., less than 384 bytes per entry including map overhead).
    // Why 384 bytes?
    // - Base slot size (GaugeKey 56B + InflightGauge 40B) = 96B.
    // - absl::flat_hash_map load factor overhead can push average to about 110B.
    // - Just after table doubling, it can peak to about 220B per item.
    // - Tag view making owned adds around 16 to 32B per item.
    // - Total peak estimate about 252B. 384 gives a generous 1.5x buffer for allocator page
    // alignment.
    EXPECT_MEMORY_LE(memory_test.consumedBytes(), NUM_ITEMS * 384);

    // 2. Remove all items
    for (int i = 0; i < NUM_ITEMS; ++i) {
      access_log_state->removeInflightGauge(names[i], std::cref(tags),
                                            Stats::Gauge::ImportMode::Accumulate, 1);
    }

    // absl::flat_hash_map is designed to not release its slots after removing entries,
    // which is why we check for such a big memory usage here (approximately 1.6 Megabytes for
    // 10,000 items). We set a threshold of 2 Megabytes here to account for this capacity and
    // allocator page alignment.
    EXPECT_LE(static_cast<int64_t>(memory_test.consumedBytes()), 2097152);

    // Destroy the object! This must release the map capacity.
    access_log_state.reset();

    // After destruction, there should be no leaks. We allow 4096 bytes for allocator caches.
    // We use EXPECT_LE directly here to bypass the strict EXPECT_GT constraint of EXPECT_MEMORY_LE.
    EXPECT_LE(memory_test.consumedBytes(), 4096);
  }
}

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
