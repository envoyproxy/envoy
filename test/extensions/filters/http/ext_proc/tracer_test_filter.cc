#include "envoy/registry/registry.h"
#include "envoy/tracing/trace_context.h"

#include "source/common/tracing/trace_context_impl.h"
#include "source/extensions/tracers/common/factory_base.h"

#include "test/extensions/filters/http/ext_proc/tracer_test_filter.pb.h"
#include "test/extensions/filters/http/ext_proc/tracer_test_filter.pb.validate.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

const Tracing::TraceContextHandler& traceParentHeader() {
  CONSTRUCT_ON_FIRST_USE(Tracing::TraceContextHandler, "traceparent");
}

struct ExpectedSpan {
  std::string operation_name;
  bool sampled;
  bool context_injected;
  std::map<std::string, std::string> tags;
  bool tested;
};

using ExpectedSpansSharedPtr = std::shared_ptr<std::vector<ExpectedSpan>>;

class Span : public Tracing::Span {
public:
  Span(const std::string& operation_name, ExpectedSpansSharedPtr& expected_spans)
      : operation_name_(operation_name), expected_spans_(expected_spans){};

  ~Span() {
    EXPECT_TRUE(finished_) << fmt::format("span not finished in operation: {}", operation_name_);
    for (auto& expect_span : *expected_spans_) {
      if (expect_span.operation_name != operation_name_) {
        continue;
      }
      EXPECT_EQ(expect_span.sampled, sampled_) << fmt::format("operation: {}", operation_name_);
      EXPECT_EQ(expect_span.context_injected, context_injected_)
          << fmt::format("operation: {}", operation_name_);

      std::string all_tags;
      for (const auto& [key, value] : tags_) {
        all_tags += fmt::format("{}: {}\n", key, value);
      }
      for (const auto& [key, want] : expect_span.tags) {
        absl::string_view got = tags_[key];
        EXPECT_EQ(want, got) << fmt::format("{}: {} not found in tags:\n{}", key, want, all_tags);
      }
      expect_span.tested = true;
      break;
    }
  }

  void setTag(absl::string_view name, absl::string_view value) {
    tags_.insert_or_assign(name.data(), value.data());
  }
  void setOperation(absl::string_view operation_name) { operation_name_ = operation_name; }
  void setSampled(bool do_sample) { sampled_ = do_sample; }

  void injectContext(Tracing::TraceContext& trace_context, const Tracing::UpstreamContext&) {
    std::string traceparent_header_value = "1";
    traceParentHeader().setRefKey(trace_context, traceparent_header_value);
    context_injected_ = true;
  }
  void setBaggage(absl::string_view, absl::string_view) { /* not implemented */
  }
  void log(SystemTime, const std::string&) { /* not implemented */
  }
  std::string getBaggage(absl::string_view) {
    /* not implemented */
    return EMPTY_STRING;
  };
  std::string getTraceId() const {
    /* not implemented */
    return EMPTY_STRING;
  };
  std::string getSpanId() const {
    /* not implemented */
    return EMPTY_STRING;
  };

  Tracing::SpanPtr spawnChild(const Tracing::Config&, const std::string& operation_name,
                              SystemTime) {
    return std::make_unique<Span>(operation_name, expected_spans_);
  }

  void finishSpan() { finished_ = true; }

private:
  std::string operation_name_;
  ExpectedSpansSharedPtr expected_spans_;

  std::map<std::string, std::string> tags_;
  bool context_injected_;
  bool sampled_;
  bool finished_;
};

class Driver : public Tracing::Driver, Logger::Loggable<Logger::Id::tracing> {
public:
  Driver(const test::integration::filters::TracerTestConfig& test_config,
         Server::Configuration::CommonFactoryContext&)
      : expected_spans_(std::make_shared<std::vector<ExpectedSpan>>()) {
    for (auto expected_span : test_config.expect_spans()) {
      ExpectedSpan span;
      span.operation_name = expected_span.operation_name();
      span.sampled = expected_span.sampled();
      span.context_injected = expected_span.context_injected();
      span.tags.insert(expected_span.tags().begin(), expected_span.tags().end());
      expected_spans_->push_back(span);
    };
  };
  // Tracing::Driver
  Tracing::SpanPtr startSpan(const Tracing::Config&, Tracing::TraceContext&,
                             const StreamInfo::StreamInfo&, const std::string& operation_name,
                             Tracing::Decision) override {

    return std::make_unique<Span>(operation_name, expected_spans_);
  };

  ~Driver() {
    for (auto& span : *expected_spans_) {
      EXPECT_TRUE(span.tested) << fmt::format("missing span with operation '{}'",
                                              span.operation_name);
    }
  };

private:
  ExpectedSpansSharedPtr expected_spans_;
};

class TracerTestFactory
    : public Tracers::Common::FactoryBase<test::integration::filters::TracerTestConfig> {
public:
  TracerTestFactory();

private:
  // FactoryBase
  Tracing::DriverSharedPtr
  createTracerDriverTyped(const test::integration::filters::TracerTestConfig& test_config,
                          Server::Configuration::TracerFactoryContext& context) override {
    return std::make_shared<Driver>(test_config, context.serverFactoryContext());
  };
};

TracerTestFactory::TracerTestFactory() : FactoryBase("tracer-test-filter") {}

REGISTER_FACTORY(TracerTestFactory, Server::Configuration::TracerFactory);

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
