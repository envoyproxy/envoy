#include <utility>
#include <vector>

#include "envoy/http/header_map.h"

#include "source/extensions/tracers/datadog/dict_util.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "datadog/optional.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

TEST(DatadogTracerDictUtilTest, RequestHeaderWriter) {
  Http::TestRequestHeaderMapImpl headers;
  RequestHeaderWriter writer{headers};

  writer.set("foo", "bar");
  writer.set("FOO", "baz");
  writer.set("sniff", "wiggle");

  auto result = headers.get(Http::LowerCaseString{"foo"});
  ASSERT_EQ(1, result.size());
  EXPECT_EQ("baz", result[0]->value().getStringView());

  result = headers.get(Http::LowerCaseString{"sniff"});
  ASSERT_EQ(1, result.size());
  EXPECT_EQ("wiggle", result[0]->value().getStringView());

  result = headers.get(Http::LowerCaseString{"missing"});
  EXPECT_EQ(0, result.size());
}

TEST(DatadogTracerDictUtilTest, ResponseHeaderReader) {
  Http::TestResponseHeaderMapImpl headers{
      {"fish", "face"},
      {"fish", "flakes"},
      {"UPPER", "case"},
  };
  ResponseHeaderReader reader{headers};

  auto result = reader.lookup("fish");
  EXPECT_EQ("face, flakes", result);

  result = reader.lookup("upper");
  EXPECT_EQ("case", result);

  result = reader.lookup("missing");
  EXPECT_EQ(datadog::tracing::nullopt, result);

  std::vector<std::pair<std::string, std::string>> expected_visitation{
      // These entries are in reverse. We'll `pop_back` as we go.
      {"upper", "case"},
      // `lookup` comma-separates duplicate headers, but `visit` does not.
      {"fish", "flakes"},
      {"fish", "face"},
  };
  reader.visit([&](const auto& key, const auto& value) {
    ASSERT_FALSE(expected_visitation.empty());
    const auto& [expected_key, expected_value] = expected_visitation.back();
    EXPECT_EQ(expected_key, key);
    EXPECT_EQ(expected_value, value);
    expected_visitation.pop_back();
  });
}

TEST(DatadogTracerDictUtilTest, TraceContextReader) {
  const Tracing::TestTraceContextImpl context{{"foo", "bar"}, {"boo", "yah"}};
  const TraceContextReader reader{context};

  auto result = reader.lookup("foo");
  EXPECT_EQ(result, "bar");
  result = reader.lookup("boo");
  EXPECT_EQ(result, "yah");
  result = reader.lookup("snark");
  EXPECT_EQ(result, datadog::tracing::nullopt);

  // `dd-trace-cpp` doesn't call `visit` when it's extracting trace context, but
  // the method is nonetheless required by the `DictReader` interface.
  reader.visit([&](const auto& key, const auto& value) {
    const auto found = context.context_map_.find(key);
    ASSERT_NE(context.context_map_.end(), found);
    EXPECT_EQ(found->second, value);
  });
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
