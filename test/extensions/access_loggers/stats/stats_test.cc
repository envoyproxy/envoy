#include "source/extensions/access_loggers/stats/stats.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

class StatsAccessLoggerTest : public testing::Test {
public:
  void initialize(std::string config_yaml = {}) {
    const std::string default_config_yaml = R"EOF(
      stat_prefix: test_stat_prefix
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
    // The gauge is a member of store_, so we need to increment the ref count to prevent it from
    // being deleted when the shared_ptr in AccessLogState is destroyed.
    store_.gauge_.incRefCount();
    store_.gauge_.name_ = "gauge";
    store_.gauge_.setTagExtractedName("gauge");
    ON_CALL(context_, statsScope()).WillByDefault(testing::ReturnRef(store_.mockScope()));
    EXPECT_CALL(store_.mockScope(), createScope_(_))
        .WillOnce(Invoke([this](const std::string& name) {
          Stats::StatNameDynamicStorage storage(name, context_.store_.symbolTable());
          scope_ = std::make_shared<NiceMock<Stats::MockScope>>(storage.statName(), store_);
          return scope_;
        }));

    logger_ = std::make_unique<StatsAccessLog>(config, context_, std::move(filter_),
                                               std::vector<Formatter::CommandParserPtr>{});
  }

  AccessLog::FilterPtr filter_;
  NiceMock<Stats::MockStore> store_;
  NiceMock<Server::Configuration::MockGenericFactoryContext> context_;
  std::shared_ptr<Stats::MockScope> scope_;
  std::unique_ptr<StatsAccessLog> logger_;
  Formatter::Context formatter_context_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(StatsAccessLoggerTest, IncorrectValueFormatter) {
  const std::string cfg = R"EOF(
    stat_prefix: test_stat_prefix
    counters:
      - stat:
          name: counter
        value_format: '%BYTES_RECEIVED%_%BYTES_SENT%'
)EOF";

  EXPECT_THROW_WITH_MESSAGE(
      initialize(cfg), EnvoyException,
      "Stats logger `value_format` string must contain exactly one substitution");
}

TEST_F(StatsAccessLoggerTest, HistogramUnits) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
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
    stat_prefix: test_stat_prefix
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
    stat_prefix: test_stat_prefix
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
    stat_prefix: test_stat_prefix
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
    stat_prefix: test_stat_prefix
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
    stat_prefix: test_stat_prefix
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
    stat_prefix: test_stat_prefix
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
    stat_prefix: test_stat_prefix
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
    stat_prefix: test_stat_prefix
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
    stat_prefix: test_stat_prefix
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

            return scope_->counterFromStatNameWithTags_(name, tags);
          }));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, GaugeNonNumberValueFormatted) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_format: '%RESPONSE_CODE_DETAILS%'
        operations:
        - log_type: NotSet
          operation_type: SET
)EOF";

  initialize(yaml);

  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::NotSet);

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
    stat_prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_format: '%BYTES_RECEIVED%'
        operations:
        - log_type: NotSet
          operation_type: SET
)EOF";

  initialize(yaml);

  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::NotSet);

  EXPECT_CALL(stream_info_, bytesReceived()).WillRepeatedly(testing::Return(42));
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::NeverImport));
  EXPECT_CALL(store_.gauge_, set(42));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, GaugeValueFixed) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
          tags:
            - name: mytag
              value_format: '%UPSTREAM_CLUSTER%'
            - name: other_tag
              value_format: '%RESPONSE_CODE%'
        value_fixed: 42
        operations:
        - log_type: NotSet
          operation_type: SET
)EOF";

  initialize(yaml);

  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::NotSet);

  absl::optional<std::string> a_number{"42"};
  EXPECT_CALL(stream_info_, responseCodeDetails()).WillRepeatedly(testing::ReturnRef(a_number));
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::NeverImport));
  EXPECT_CALL(store_.gauge_, set(42));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, GaugeOperationTypeSet) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 42
        operations:
        - log_type: NotSet
          operation_type: SET
)EOF";
  initialize(yaml);

  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::NotSet);

  absl::optional<std::string> a_number{"42"};
  EXPECT_CALL(stream_info_, responseCodeDetails()).WillRepeatedly(testing::ReturnRef(a_number));
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::NeverImport));
  EXPECT_CALL(store_.gauge_, set(42));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, GaugeBothFormatAndFixed) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_format: '%BYTES_RECEIVED%'
        value_fixed: 1
        operations:
        - log_type: NotSet
          operation_type: SET
)EOF";

  EXPECT_THROW_WITH_MESSAGE(
      initialize(yaml), EnvoyException,
      "Stats logger cannot have both `value_format` and `value_fixed` configured.");
}

TEST_F(StatsAccessLoggerTest, GaugeNoValueConfig) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        operations:
        - log_type: NotSet
          operation_type: SET
)EOF";
  EXPECT_THROW_WITH_MESSAGE(initialize(yaml), EnvoyException,
                            "Stats logger gauge must have either `value_format` or `value_fixed`.");
}

TEST_F(StatsAccessLoggerTest, GaugeBothSetAndAddSubtract) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 42
        operations:
        - log_type: DownstreamStart
          operation_type: PAIRED_ADD
        - log_type: DownstreamEnd
          operation_type: SET
)EOF";
  EXPECT_THROW_WITH_MESSAGE(
      initialize(yaml), EnvoyException,
      "Stats logger gauge must have exactly one PAIRED_ADD and one PAIRED_SUBTRACT operation "
      "defined if either is present.");
}

TEST_F(StatsAccessLoggerTest, GaugeMultipleAdd) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 42
        operations:
        - log_type: DownstreamStart
          operation_type: PAIRED_ADD
        - log_type: DownstreamEnd
          operation_type: PAIRED_ADD
)EOF";
  EXPECT_THROW_WITH_MESSAGE(
      initialize(yaml), EnvoyException,
      "Stats logger gauge must have exactly one PAIRED_ADD and one PAIRED_SUBTRACT operation "
      "defined if either is present.");
}

TEST_F(StatsAccessLoggerTest, GaugeMissingSubtract) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 42
        operations:
        - log_type: DownstreamStart
          operation_type: PAIRED_ADD
)EOF";
  EXPECT_THROW_WITH_MESSAGE(
      initialize(yaml), EnvoyException,
      "Stats logger gauge must have exactly one PAIRED_ADD and one PAIRED_SUBTRACT operation "
      "defined if either is present.");
}

TEST_F(StatsAccessLoggerTest, GaugeUnspecifiedOperation) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 42
        operations:
        - log_type: DownstreamStart
          operation_type: UNSPECIFIED
)EOF";
  EXPECT_THROW_WITH_MESSAGE(initialize(yaml), EnvoyException,
                            "Stats logger gauge operation cannot be UNSPECIFIED.");
}

TEST_F(StatsAccessLoggerTest, GaugeNeitherSetNorAddSubtract) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
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
    stat_prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 1
        operations:
        - log_type: DownstreamStart
          operation_type: PAIRED_ADD
        - log_type: DownstreamEnd
          operation_type: PAIRED_SUBTRACT
)EOF";
  initialize(yaml);

  // Case 1: AccessLogType matches neither -> no change
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::NotSet);
  EXPECT_CALL(store_, gauge(_, _)).Times(0);
  logger_->log(formatter_context_, stream_info_);
  testing::Mock::VerifyAndClearExpectations(&store_);
  testing::Mock::VerifyAndClearExpectations(&store_.gauge_);

  // Case 2: AccessLogType matches subtract_at but no prior add -> no change
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate));
  EXPECT_CALL(store_.gauge_, add(_)).Times(0);
  EXPECT_CALL(store_.gauge_, sub(_)).Times(0);
  logger_->log(formatter_context_, stream_info_);
  testing::Mock::VerifyAndClearExpectations(&store_);
  testing::Mock::VerifyAndClearExpectations(&store_.gauge_);

  // Case 3: AccessLogType matches add_at -> add
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamStart);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate));
  EXPECT_CALL(store_.gauge_, add(1));
  logger_->log(formatter_context_, stream_info_);
  testing::Mock::VerifyAndClearExpectations(&store_);
  testing::Mock::VerifyAndClearExpectations(&store_.gauge_);

  // Case 4: AccessLogType matches subtract_at after add -> subtract
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate));
  EXPECT_CALL(store_.gauge_, sub(1));
  logger_->log(formatter_context_, stream_info_);
  testing::Mock::VerifyAndClearExpectations(&store_);
  testing::Mock::VerifyAndClearExpectations(&store_.gauge_);

  // Case 5: AccessLogType matches subtract_at again -> no change (already removed from inflight)
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate));
  EXPECT_CALL(store_.gauge_, sub(1)).Times(0);
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, PairedSubtractIgnoresConfiguredValue) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 10
        operations:
        - log_type: DownstreamStart
          operation_type: PAIRED_ADD
        - log_type: DownstreamEnd
          operation_type: PAIRED_SUBTRACT
)EOF";
  initialize(yaml);

  // Trigger ADD with value 10
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamStart);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate));
  EXPECT_CALL(store_.gauge_, add(10));
  logger_->log(formatter_context_, stream_info_);

  // Trigger SUBTRACT. Should still subtract 10.
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate));
  EXPECT_CALL(store_.gauge_, sub(10));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, DestructionSubtractsRemainingValue) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
    gauges:
      - stat:
          name: gauge
        value_fixed: 10
        operations:
        - log_type: DownstreamStart
          operation_type: PAIRED_ADD
        - log_type: DownstreamEnd
          operation_type: PAIRED_SUBTRACT
)EOF";
  initialize(yaml);

  // Trigger ADD using a local StreamInfo so we can control its lifetime.
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamStart);

  NiceMock<StreamInfo::MockStreamInfo> local_stream_info;

  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate));
  EXPECT_CALL(store_.gauge_, add(10));
  logger_->log(formatter_context_, local_stream_info);

  // Expect subtraction on destruction
  EXPECT_CALL(store_.gauge_, sub(10));

  // local_stream_info goes out of scope here.
}

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
