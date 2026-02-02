#include "source/extensions/access_loggers/stats/stats.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
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

TEST_F(StatsAccessLoggerTest, DropStatAction) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
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
                  tag_name: foo
              exact_match_map:
                map:
                  "bar":
                    action:
                      name: generic_stat_action
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.matching.common_actions.stats.v3.StatAction
                        drop_stat: {}
        value_fixed: 1
)EOF";

  initialize(yaml);

  // Case 1: Filter matches (tag foo=bar), so drop action is executed.
  EXPECT_CALL(store_, counter(_)).Times(0);
  logger_->log(formatter_context_, stream_info_);

  const std::string yaml2 = R"EOF(
    stat_prefix: test_stat_prefix
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
                  tag_name: foo
              exact_match_map:
                map:
                  "bar":
                    action:
                      name: generic_stat_action
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.matching.common_actions.stats.v3.StatAction
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
    stat_prefix: test_stat_prefix
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
                  tag_name: foo
              exact_match_map:
                map:
                  "bar":
                    action:
                      name: generic_stat_action
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.matching.common_actions.stats.v3.StatAction
                        drop_stat: {}
        unit: Bytes
        value_format: '%BYTES_RECEIVED%'
)EOF";

  initialize(yaml);

  // Case 1: Filter matches (tag foo=bar), so drop action is executed.
  EXPECT_CALL(store_, histogram(_, _)).Times(0);
  logger_->log(formatter_context_, stream_info_);

  const std::string yaml2 = R"EOF(
    stat_prefix: test_stat_prefix
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
                  tag_name: foo
              exact_match_map:
                map:
                  "bar":
                    action:
                      name: generic_stat_action
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.matching.common_actions.stats.v3.StatAction
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

TEST_F(StatsAccessLoggerTest, StatTagFilterErrorType) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
    counters:
      - stat:
          name: proxy-network-errors
          tags:
            - name: error.type
              value_format: "-"
          rules:
            matcher_tree:
              input:
                name: stat_tag_value_input
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.stats.v3.StatTagValueInput
                  tag_name: error.type
              exact_match_map:
                map:
                  "-":
                    action:
                      name: generic_stat_action
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.matching.common_actions.stats.v3.StatAction
                        drop_stat: {}
        value_fixed: 1
)EOF";

  initialize(yaml);

  // Case 1: error.type is "-", so drop action is executed.
  EXPECT_CALL(store_, counter(_)).Times(0);
  logger_->log(formatter_context_, stream_info_);

  const std::string yaml2 = R"EOF(
    stat_prefix: test_stat_prefix
    counters:
      - stat:
          name: proxy-network-errors
          tags:
            - name: error.type
              value_format: "UH"
          rules:
            matcher_tree:
              input:
                name: stat_tag_value_input
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.stats.v3.StatTagValueInput
                  tag_name: error.type
              exact_match_map:
                map:
                  "-":
                    action:
                      name: generic_stat_action
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.matching.common_actions.stats.v3.StatAction
                        drop_stat: {}
        value_fixed: 1
)EOF";
  initialize(yaml2);

  // Case 2: error.type is "UH", so drop action is NOT executed.
  EXPECT_CALL(store_, counter(_));
  EXPECT_CALL(store_.counter_, add(1));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, StatTagFilterInsertTag) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
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
                  tag_name: foo
              exact_match_map:
                map:
                  "bar":
                    action:
                      name: generic_stat_action
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.matching.common_actions.stats.v3.StatAction
                        insert_tag:
                          tag_name: foo
                          tag_value: baz
        value_fixed: 1
)EOF";

  initialize(yaml);

  // Case 1: Filter matches (tag foo=bar), so insert tag action is executed.
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
    stat_prefix: test_stat_prefix
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
                  tag_name: foo
              exact_match_map:
                map:
                  "bar":
                    action:
                      name: generic_stat_action
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.matching.common_actions.stats.v3.StatAction
                        drop_tag:
                          target_tag_name: foo
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

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
