#include "envoy/stats/sink.h"

#include "source/common/stats/allocator_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/extensions/access_loggers/stats/stats.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

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
    auto* gauge = new NiceMock<MockGaugeWithTags>();
    gauge_ = gauge;
    gauge_ptr_ = Stats::GaugeSharedPtr(gauge_);
    gauge_->name_ = "gauge";
    gauge_->setTagExtractedName("gauge");
    ON_CALL(store_, gauge(_, _)).WillByDefault(testing::ReturnRef(*gauge_));

    ON_CALL(context_, statsScope()).WillByDefault(testing::ReturnRef(store_.mockScope()));
    EXPECT_CALL(store_.mockScope(), createScope_(_))
        .WillOnce(Invoke([this](const std::string& name) {
          scope_name_storage_ =
              std::make_unique<Stats::StatNameDynamicStorage>(name, context_.store_.symbolTable());
          auto scope = std::make_shared<NiceMock<MockScopeWithGauge>>(
              scope_name_storage_->statName(), store_);
          ON_CALL(*scope, gaugeFromStatNameWithTags(_, _, _))
              .WillByDefault(Invoke(
                  [scope_ptr = scope.get()](const Stats::StatName& name,
                                            Stats::StatNameTagVectorOptConstRef tags,
                                            Stats::Gauge::ImportMode import_mode) -> Stats::Gauge& {
                    return scope_ptr->Stats::MockScope::gaugeFromStatNameWithTags(name, tags,
                                                                                  import_mode);
                  }));
          scope_ = scope;
          return scope_;
        }));

    logger_ = std::make_unique<StatsAccessLog>(config, context_, std::move(filter_),
                                               std::vector<Formatter::CommandParserPtr>{});
  }

  AccessLog::FilterPtr filter_;
  NiceMock<Stats::MockStore> store_;
  NiceMock<Server::Configuration::MockGenericFactoryContext> context_;
  std::shared_ptr<Stats::MockScope> scope_;
  std::unique_ptr<Stats::StatNameDynamicStorage> scope_name_storage_;
  std::unique_ptr<StatsAccessLog> logger_;
  Formatter::Context formatter_context_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Stats::GaugeSharedPtr gauge_ptr_;
  Stats::MockGauge* gauge_;
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
    stat_prefix: test_stat_prefix
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
    stat_prefix: test_stat_prefix
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
    stat_prefix: test_stat_prefix
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
    stat_prefix: test_stat_prefix
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
    stat_prefix: test_stat_prefix
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
    stat_prefix: test_stat_prefix
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

  // Case 2: AccessLogType matches subtract_at but no prior add -> no change
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate));
  EXPECT_CALL(*gauge_, add(_)).Times(0);
  EXPECT_CALL(*gauge_, sub(_)).Times(0);
  logger_->log(formatter_context_, stream_info_);
  testing::Mock::VerifyAndClearExpectations(&store_);
  testing::Mock::VerifyAndClearExpectations(&*gauge_);

  // Case 3: AccessLogType matches add_at -> add
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamStart);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate));
  EXPECT_CALL(*gauge_, add(1));
  logger_->log(formatter_context_, stream_info_);
  testing::Mock::VerifyAndClearExpectations(&store_);
  testing::Mock::VerifyAndClearExpectations(&*gauge_);

  // Case 4: AccessLogType matches subtract_at after add -> subtract
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate));
  EXPECT_CALL(*gauge_, sub(1));
  logger_->log(formatter_context_, stream_info_);
  testing::Mock::VerifyAndClearExpectations(&store_);
  testing::Mock::VerifyAndClearExpectations(&*gauge_);

  // Case 5: AccessLogType matches subtract_at again -> no change (already removed from inflight)
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate));
  EXPECT_CALL(*gauge_, sub(1)).Times(0);
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, PairedSubtractIgnoresConfiguredValue) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
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
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate));
  EXPECT_CALL(*gauge_, add(10));
  logger_->log(formatter_context_, stream_info_);

  // Trigger SUBTRACT. Should still subtract 10.
  formatter_context_.setAccessLogType(envoy::data::accesslog::v3::AccessLogType::DownstreamEnd);
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate));
  EXPECT_CALL(*gauge_, sub(10));
  logger_->log(formatter_context_, stream_info_);
}

TEST_F(StatsAccessLoggerTest, DestructionSubtractsRemainingValue) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
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

  // Called once on log() and once on destruction.
  EXPECT_CALL(store_, gauge(_, Stats::Gauge::ImportMode::Accumulate)).Times(2);
  EXPECT_CALL(*gauge_, add(10));
  logger_->log(formatter_context_, local_stream_info);

  // Expect subtraction on destruction
  EXPECT_CALL(*gauge_, sub(10));

  // local_stream_info goes out of scope here.
}

TEST_F(StatsAccessLoggerTest, AccessLogStateDestructorReconstructsGauge) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
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
      .WillOnce(Invoke([&](const Stats::StatName& name, Stats::StatNameTagVectorOptConstRef tags,
                           Stats::Gauge::ImportMode) -> Stats::Gauge& {
        saved_name = name;
        if (tags) {
          for (const auto& tag : tags->get()) {
            saved_tags_strs.emplace_back(store_.symbolTable().toString(tag.first),
                                         store_.symbolTable().toString(tag.second));
          }
        }
        EXPECT_FALSE(saved_tags_strs.empty());
        auto* gauge_with_tags = dynamic_cast<MockGaugeWithTags*>(gauge_);
        EXPECT_TRUE(gauge_with_tags != nullptr);
        gauge_with_tags->setTags(tags->get(), store_.symbolTable());
        return *gauge_;
      }));
  EXPECT_CALL(*gauge_, add(10));
  logger_->log(formatter_context_, local_stream_info);

  // Simulate eviction from scope (or just verify lookup happens again)
  // The destructor of AccessLogState should call gaugeFromStatNameWithTags again.
  EXPECT_CALL(*mock_scope, gaugeFromStatNameWithTags(_, _, Stats::Gauge::ImportMode::Accumulate))
      .WillOnce(Invoke([&](const Stats::StatName& name, Stats::StatNameTagVectorOptConstRef tags,
                           Stats::Gauge::ImportMode) -> Stats::Gauge& {
        EXPECT_EQ(name, saved_name);
        EXPECT_TRUE(tags.has_value());
        if (tags) {
          const auto& tags_vec = tags->get();
          // Detailed comparison
          EXPECT_EQ(tags_vec.size(), 2);
          if (tags_vec.size() == 2) {
            EXPECT_EQ(store_.symbolTable().toString(tags_vec[0].first), "tag_name");
            EXPECT_EQ(store_.symbolTable().toString(tags_vec[0].second), "200");
            EXPECT_EQ(store_.symbolTable().toString(tags_vec[1].first), "another_tag");
            EXPECT_EQ(store_.symbolTable().toString(tags_vec[1].second), "value_fixed");
          }
        }
        return *gauge_;
      }));
  EXPECT_CALL(*gauge_, sub(10));

  // local_stream_info goes out of scope here, triggering AccessLogState destructor.
}

TEST_F(StatsAccessLoggerTest, GaugeNotSet) {
  const std::string yaml = R"EOF(
    stat_prefix: test_stat_prefix
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

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
