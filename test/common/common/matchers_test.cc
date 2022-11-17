#include "envoy/common/exception.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/type/matcher/v3/metadata.pb.h"
#include "envoy/type/matcher/v3/string.pb.h"
#include "envoy/type/matcher/v3/value.pb.h"

#include "source/common/common/matchers.h"
#include "source/common/config/metadata.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stream_info/filter_state_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {
namespace {

TEST(MetadataTest, MatchNullValue) {
  envoy::config::core::v3::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label")
      .set_string_value("test");
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_null_value(ProtobufWkt::NullValue::NULL_VALUE);

  envoy::type::matcher::v3::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_null_match();
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
}

TEST(MetadataTest, MatchDoubleValue) {
  envoy::config::core::v3::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label")
      .set_string_value("test");
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_number_value(9);

  envoy::type::matcher::v3::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_double_match()->set_exact(1);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_double_match()->set_exact(9);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  auto r = matcher.mutable_value()->mutable_double_match()->mutable_range();
  r->set_start(9.1);
  r->set_end(10);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  r = matcher.mutable_value()->mutable_double_match()->mutable_range();
  r->set_start(8.9);
  r->set_end(9);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  r = matcher.mutable_value()->mutable_double_match()->mutable_range();
  r->set_start(9);
  r->set_end(9.1);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
}

TEST(MetadataTest, MatchStringExactValue) {
  envoy::config::core::v3::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label")
      .set_string_value("test");
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_string_value("prod");

  envoy::type::matcher::v3::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_string_match()->set_exact("prod");
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
}

TEST(MetadataTest, MatchStringPrefixValue) {
  envoy::config::core::v3::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label")
      .set_string_value("test");
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_string_value("prodabc");

  envoy::type::matcher::v3::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_string_match()->set_prefix("prodx");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_string_match()->set_prefix("prod");
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
}

TEST(MetadataTest, MatchStringSuffixValue) {
  envoy::config::core::v3::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label")
      .set_string_value("test");
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_string_value("abcprod");

  envoy::type::matcher::v3::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_string_match()->set_suffix("prodx");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_string_match()->set_suffix("prod");
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
}

TEST(MetadataTest, MatchStringContainsValue) {
  envoy::config::core::v3::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label")
      .set_string_value("test");
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_string_value("abcprodef");

  envoy::type::matcher::v3::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_string_match()->set_contains("pride");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_string_match()->set_contains("prod");
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
}

TEST(MetadataTest, MatchBoolValue) {
  envoy::config::core::v3::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label")
      .set_string_value("test");
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_bool_value(true);

  envoy::type::matcher::v3::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->set_bool_match(false);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->set_bool_match(true);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
}

TEST(MetadataTest, MatchPresentValue) {
  envoy::config::core::v3::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label")
      .set_string_value("test");
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_number_value(1);

  envoy::type::matcher::v3::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->set_present_match(false);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->set_present_match(true);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  matcher.clear_path();
  matcher.add_path()->set_key("unknown");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
}

// Helper function to retrieve the reference of an entry in a ListMatcher from a MetadataMatcher.
envoy::type::matcher::v3::ValueMatcher*
listMatchEntry(envoy::type::matcher::v3::MetadataMatcher* matcher) {
  return matcher->mutable_value()->mutable_list_match()->mutable_one_of();
}

TEST(MetadataTest, MatchStringListValue) {
  envoy::config::core::v3::Metadata metadata;
  ProtobufWkt::Value& metadataValue =
      Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "groups");
  ProtobufWkt::ListValue* values = metadataValue.mutable_list_value();
  values->add_values()->set_string_value("first");
  values->add_values()->set_string_value("second");
  values->add_values()->set_string_value("third");

  envoy::type::matcher::v3::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.a");
  matcher.add_path()->set_key("groups");

  listMatchEntry(&matcher)->mutable_string_match()->set_exact("second");
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->mutable_string_match()->set_prefix("fi");
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->mutable_string_match()->set_suffix("rd");
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->mutable_string_match()->set_exact("fourth");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->mutable_string_match()->set_prefix("none");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  values->clear_values();
  metadataValue.Clear();
}

TEST(MetadataTest, MatchBoolListValue) {
  envoy::config::core::v3::Metadata metadata;
  ProtobufWkt::Value& metadataValue =
      Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "groups");
  ProtobufWkt::ListValue* values = metadataValue.mutable_list_value();
  values->add_values()->set_bool_value(false);
  values->add_values()->set_bool_value(false);

  envoy::type::matcher::v3::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.a");
  matcher.add_path()->set_key("groups");

  listMatchEntry(&matcher)->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->set_bool_match(true);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->set_bool_match(false);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  values->clear_values();
  metadataValue.Clear();
}

TEST(MetadataTest, MatchDoubleListValue) {
  envoy::config::core::v3::Metadata metadata;
  ProtobufWkt::Value& metadataValue =
      Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "groups");
  ProtobufWkt::ListValue* values = metadataValue.mutable_list_value();
  values->add_values()->set_number_value(10);
  values->add_values()->set_number_value(23);

  envoy::type::matcher::v3::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.a");
  matcher.add_path()->set_key("groups");

  listMatchEntry(&matcher)->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->set_bool_match(true);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->mutable_double_match()->set_exact(9);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->mutable_double_match()->set_exact(10);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  auto r = listMatchEntry(&matcher)->mutable_double_match()->mutable_range();
  r->set_start(10);
  r->set_end(15);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  r = listMatchEntry(&matcher)->mutable_double_match()->mutable_range();
  r->set_start(20);
  r->set_end(24);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  r = listMatchEntry(&matcher)->mutable_double_match()->mutable_range();
  r->set_start(24);
  r->set_end(26);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  values->clear_values();
  metadataValue.Clear();
}

TEST(MetadataTest, InvertMatch) {
  envoy::config::core::v3::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.x", "label")
      .set_string_value("prod");

  envoy::type::matcher::v3::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.x");
  matcher.add_path()->set_key("label");
  matcher.set_invert(true);

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_string_match()->set_exact("prod");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
}

TEST(StringMatcher, ExactMatchIgnoreCase) {
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact("exact");
  EXPECT_TRUE(Matchers::StringMatcherImpl(matcher).match("exact"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("EXACT"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("exacz"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("other"));

  matcher.set_ignore_case(true);
  EXPECT_TRUE(Matchers::StringMatcherImpl(matcher).match("exact"));
  EXPECT_TRUE(Matchers::StringMatcherImpl(matcher).match("EXACT"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("exacz"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("other"));
}

TEST(StringMatcher, PrefixMatchIgnoreCase) {
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_prefix("prefix");
  EXPECT_TRUE(Matchers::StringMatcherImpl(matcher).match("prefix-abc"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("PREFIX-ABC"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("prefiz-abc"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("other"));

  matcher.set_ignore_case(true);
  EXPECT_TRUE(Matchers::StringMatcherImpl(matcher).match("prefix-abc"));
  EXPECT_TRUE(Matchers::StringMatcherImpl(matcher).match("PREFIX-ABC"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("prefiz-abc"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("other"));
}

TEST(StringMatcher, SuffixMatchIgnoreCase) {
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_suffix("suffix");
  EXPECT_TRUE(Matchers::StringMatcherImpl(matcher).match("abc-suffix"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("ABC-SUFFIX"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("abc-suffiz"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("other"));

  matcher.set_ignore_case(true);
  EXPECT_TRUE(Matchers::StringMatcherImpl(matcher).match("abc-suffix"));
  EXPECT_TRUE(Matchers::StringMatcherImpl(matcher).match("ABC-SUFFIX"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("abc-suffiz"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("other"));
}

TEST(StringMatcher, ContainsMatchIgnoreCase) {
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_contains("contained-str");
  EXPECT_TRUE(Matchers::StringMatcherImpl(matcher).match("abc-contained-str-def"));
  EXPECT_TRUE(Matchers::StringMatcherImpl(matcher).match("contained-str"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("ABC-Contained-Str-DEF"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("abc-container-int-def"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("other"));

  matcher.set_ignore_case(true);
  EXPECT_TRUE(Matchers::StringMatcherImpl(matcher).match("abc-contained-str-def"));
  EXPECT_TRUE(Matchers::StringMatcherImpl(matcher).match("abc-cOnTaInEd-str-def"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("abc-ContAineR-str-def"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("other"));
}

TEST(StringMatcher, SafeRegexValue) {
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.mutable_safe_regex()->mutable_google_re2();
  matcher.mutable_safe_regex()->set_regex("foo.*");
  EXPECT_TRUE(Matchers::StringMatcherImpl(matcher).match("foo"));
  EXPECT_TRUE(Matchers::StringMatcherImpl(matcher).match("foobar"));
  EXPECT_FALSE(Matchers::StringMatcherImpl(matcher).match("bar"));
}

TEST(StringMatcher, SafeRegexValueIgnoreCase) {
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_ignore_case(true);
  matcher.mutable_safe_regex()->mutable_google_re2();
  matcher.mutable_safe_regex()->set_regex("foo");
  EXPECT_THROW_WITH_MESSAGE(Matchers::StringMatcherImpl(matcher).match("foo"), EnvoyException,
                            "ignore_case has no effect for safe_regex.");
}

TEST(PathMatcher, MatchExactPath) {
  const auto matcher = Envoy::Matchers::PathMatcher::createExact("/exact", false);

  EXPECT_TRUE(matcher->match("/exact"));
  EXPECT_TRUE(matcher->match("/exact?param=val"));
  EXPECT_TRUE(matcher->match("/exact#fragment"));
  EXPECT_TRUE(matcher->match("/exact#fragment?param=val"));
  EXPECT_FALSE(matcher->match("/EXACT"));
  EXPECT_FALSE(matcher->match("/exacz"));
  EXPECT_FALSE(matcher->match("/exact-abc"));
  EXPECT_FALSE(matcher->match("/exacz?/exact"));
  EXPECT_FALSE(matcher->match("/exacz#/exact"));
}

TEST(PathMatcher, MatchExactPathIgnoreCase) {
  const auto matcher = Envoy::Matchers::PathMatcher::createExact("/exact", true);

  EXPECT_TRUE(matcher->match("/exact"));
  EXPECT_TRUE(matcher->match("/EXACT"));
  EXPECT_TRUE(matcher->match("/exact?param=val"));
  EXPECT_TRUE(matcher->match("/Exact#fragment"));
  EXPECT_TRUE(matcher->match("/EXACT#fragment?param=val"));
  EXPECT_FALSE(matcher->match("/exacz"));
  EXPECT_FALSE(matcher->match("/exact-abc"));
  EXPECT_FALSE(matcher->match("/exacz?/exact"));
  EXPECT_FALSE(matcher->match("/exacz#/exact"));
}

TEST(PathMatcher, MatchPrefixPath) {
  const auto matcher = Envoy::Matchers::PathMatcher::createPrefix("/prefix", false);

  EXPECT_TRUE(matcher->match("/prefix"));
  EXPECT_TRUE(matcher->match("/prefix-abc"));
  EXPECT_TRUE(matcher->match("/prefix?param=val"));
  EXPECT_TRUE(matcher->match("/prefix#fragment"));
  EXPECT_TRUE(matcher->match("/prefix#fragment?param=val"));
  EXPECT_FALSE(matcher->match("/PREFIX"));
  EXPECT_FALSE(matcher->match("/prefiz"));
  EXPECT_FALSE(matcher->match("/prefiz?/prefix"));
  EXPECT_FALSE(matcher->match("/prefiz#/prefix"));
}

TEST(PathMatcher, MatchPrefixPathIgnoreCase) {
  const auto matcher = Envoy::Matchers::PathMatcher::createPrefix("/prefix", true);

  EXPECT_TRUE(matcher->match("/prefix"));
  EXPECT_TRUE(matcher->match("/prefix-abc"));
  EXPECT_TRUE(matcher->match("/Prefix?param=val"));
  EXPECT_TRUE(matcher->match("/Prefix#fragment"));
  EXPECT_TRUE(matcher->match("/PREFIX#fragment?param=val"));
  EXPECT_TRUE(matcher->match("/PREFIX"));
  EXPECT_FALSE(matcher->match("/prefiz"));
  EXPECT_FALSE(matcher->match("/prefiz?/prefix"));
  EXPECT_FALSE(matcher->match("/prefiz#/prefix"));
}

TEST(PathMatcher, MatchSuffixPath) {
  envoy::type::matcher::v3::PathMatcher matcher;
  matcher.mutable_path()->set_suffix("suffix");

  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/suffix"));
  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/abc-suffix"));
  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/suffix?param=val"));
  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/suffix#fragment"));
  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/suffix#fragment?param=val"));
  EXPECT_FALSE(Matchers::PathMatcher(matcher).match("/suffiz"));
  EXPECT_FALSE(Matchers::PathMatcher(matcher).match("/suffiz?param=suffix"));
  EXPECT_FALSE(Matchers::PathMatcher(matcher).match("/suffiz#suffix"));
}

TEST(PathMatcher, MatchContainsPath) {
  envoy::type::matcher::v3::PathMatcher matcher;
  matcher.mutable_path()->set_contains("contains");

  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/contains"));
  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/abc-contains"));
  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/contains-abc"));
  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/abc-contains-def"));
  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/abc-contains-def?param=val"));
  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/abc-contains-def#fragment"));
  EXPECT_FALSE(Matchers::PathMatcher(matcher).match("/abc-def#containsfragment?param=contains"));
  EXPECT_FALSE(Matchers::PathMatcher(matcher).match("/abc-curtains-def"));
}

TEST(PathMatcher, MatchRegexPath) {
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.mutable_safe_regex()->mutable_google_re2();
  matcher.mutable_safe_regex()->set_regex(".*regex.*");

  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/regex"));
  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/regex/xyz"));
  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/xyz/regex"));
  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/regex?param=val"));
  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/regex#fragment"));
  EXPECT_TRUE(Matchers::PathMatcher(matcher).match("/regex#fragment?param=val"));
  EXPECT_FALSE(Matchers::PathMatcher(matcher).match("/regez"));
  EXPECT_FALSE(Matchers::PathMatcher(matcher).match("/regez?param=regex"));
  EXPECT_FALSE(Matchers::PathMatcher(matcher).match("/regez#regex"));
}

TEST(FilterStateMatcher, MatchAbsentFilterState) {
  envoy::type::matcher::v3::FilterStateMatcher matcher;
  matcher.set_key("test.key");
  matcher.mutable_string_match()->set_exact("exact");
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  EXPECT_FALSE(Matchers::FilterStateMatcher(matcher).match(filter_state));
}

class TestObject : public StreamInfo::FilterState::Object {
public:
  TestObject(absl::optional<std::string> value) : value_(value) {}
  absl::optional<std::string> serializeAsString() const override { return value_; }

private:
  absl::optional<std::string> value_;
};

TEST(FilterStateMatcher, MatchFilterStateWithoutString) {
  const std::string key = "test.key";
  envoy::type::matcher::v3::FilterStateMatcher matcher;
  matcher.set_key(key);
  matcher.mutable_string_match()->set_exact("exact");
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state.setData(key, std::make_shared<TestObject>(absl::nullopt),
                       StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_FALSE(Matchers::FilterStateMatcher(matcher).match(filter_state));
}

TEST(FilterStateMatcher, MatchFilterStateDifferentString) {
  const std::string key = "test.key";
  const std::string value = "exact_value";
  envoy::type::matcher::v3::FilterStateMatcher matcher;
  matcher.set_key(key);
  matcher.mutable_string_match()->set_exact(value);
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state.setData(key,
                       std::make_shared<TestObject>(absl::make_optional<std::string>("different")),
                       StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_FALSE(Matchers::FilterStateMatcher(matcher).match(filter_state));
}

TEST(FilterStateMatcher, MatchFilterState) {
  const std::string key = "test.key";
  const std::string value = "exact_value";
  envoy::type::matcher::v3::FilterStateMatcher matcher;
  matcher.set_key(key);
  matcher.mutable_string_match()->set_exact(value);
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state.setData(key, std::make_shared<TestObject>(absl::make_optional<std::string>(value)),
                       StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_TRUE(Matchers::FilterStateMatcher(matcher).match(filter_state));
}

} // namespace
} // namespace Matcher
} // namespace Envoy
