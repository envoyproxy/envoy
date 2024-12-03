#include <exception>
#include <memory>

#include "envoy/config/common/matcher/v3/matcher.pb.validate.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/registry/registry.h"

#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/matching/http/metadata_input/meta_input.h"
#include "source/extensions/matching/input_matchers/metadata/config.h"
#include "source/extensions/matching/input_matchers/metadata/matcher.h"

#include "test/common/matcher/test_utility.h"
#include "test/mocks/matcher/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "xds/type/matcher/v3/matcher.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Metadata {

constexpr absl::string_view kFilterNamespace = "meta_matcher";
constexpr absl::string_view kMetadataKey = "service_name";
constexpr absl::string_view kMetadataValue = "test_service";

class MetadataMatcherTest : public ::testing::Test {
public:
  MetadataMatcherTest()
      : inject_action_(action_factory_),
        data_(Envoy::Http::Matching::HttpMatchingDataImpl(stream_info_)) {}

  ::Envoy::Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData>
  buildMatcherTree(bool invert = false) {
    envoy::extensions::matching::input_matchers::metadata::v3::Metadata meta_matcher;
    meta_matcher.mutable_value()->mutable_string_match()->set_exact(kMetadataValue);
    meta_matcher.set_invert(invert);

    xds::type::matcher::v3::Matcher matcher;
    auto* inner_matcher = matcher.mutable_matcher_list()->add_matchers();
    auto* single_predicate = inner_matcher->mutable_predicate()->mutable_single_predicate();

    envoy::extensions::matching::common_inputs::network::v3::DynamicMetadataInput metadata_input;
    metadata_input.set_filter(kFilterNamespace);
    metadata_input.mutable_path()->Add()->set_key(kMetadataKey);
    single_predicate->mutable_input()->set_name("envoy.matching.inputs.dynamic_metadata");
    single_predicate->mutable_input()->mutable_typed_config()->PackFrom(metadata_input);

    auto* custom_matcher = single_predicate->mutable_custom_match();
    custom_matcher->mutable_typed_config()->PackFrom(meta_matcher);

    xds::type::matcher::v3::Matcher::OnMatch on_match;
    std::string on_match_config = R"EOF(
    action:
      name: test_action
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: match!!
  )EOF";
    MessageUtil::loadFromYaml(on_match_config, on_match,
                              ProtobufMessage::getStrictValidationVisitor());

    inner_matcher->mutable_on_match()->MergeFrom(on_match);

    auto string_factory_on_match = ::Envoy::Matcher::TestDataInputStringFactory("value");

    ::Envoy::Matcher::MockMatchTreeValidationVisitor<Envoy::Http::HttpMatchingData>
        validation_visitor;
    EXPECT_CALL(validation_visitor,
                performDataInputValidation(
                    _, "type.googleapis.com/"
                       "envoy.extensions.matching.common_inputs.network.v3.DynamicMetadataInput"));
    ::Envoy::Matcher::MatchTreeFactory<Envoy::Http::HttpMatchingData, absl::string_view>
        matcher_factory(context_, factory_context_, validation_visitor);
    auto match_tree = matcher_factory.create(matcher);

    return match_tree();
  }

  void setDynamicMetadata(const std::string& namespace_str, const std::string& metadata_key,
                          const std::string& metadata_value) {
    Envoy::Config::Metadata::mutableMetadataValue(stream_info_.metadata_, namespace_str,
                                                  metadata_key)
        .set_string_value(metadata_value);
  }

  ::Envoy::Matcher::StringActionFactory action_factory_;
  Registry::InjectFactory<Envoy::Matcher::ActionFactory<absl::string_view>> inject_action_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  absl::string_view context_ = "";
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  envoy::config::core::v3::Metadata metadata_;

  Envoy::Http::Matching::HttpMatchingDataImpl data_;
};

TEST_F(MetadataMatcherTest, DynamicMetadataMatched) {
  setDynamicMetadata(std::string(kFilterNamespace), std::string(kMetadataKey),
                     std::string(kMetadataValue));
  Envoy::Http::Matching::HttpMatchingDataImpl data =
      Envoy::Http::Matching::HttpMatchingDataImpl(stream_info_);
  auto matcher_tree = buildMatcherTree();
  const auto result = matcher_tree->match(data_);
  // The match was complete, match found.
  EXPECT_EQ(result.match_state_, ::Envoy::Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(MetadataMatcherTest, DynamicMetadataNotMatched) {
  setDynamicMetadata(std::string(kFilterNamespace), std::string(kMetadataKey), "wrong_service");
  Envoy::Http::Matching::HttpMatchingDataImpl data =
      Envoy::Http::Matching::HttpMatchingDataImpl(stream_info_);
  auto matcher_tree = buildMatcherTree();

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found.
  EXPECT_EQ(result.match_state_, ::Envoy::Matcher::MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_, absl::nullopt);
}

TEST_F(MetadataMatcherTest, DynamicMetadataNotMatchedWithInvert) {
  setDynamicMetadata(std::string(kFilterNamespace), std::string(kMetadataKey), "wrong_service");
  Envoy::Http::Matching::HttpMatchingDataImpl data =
      Envoy::Http::Matching::HttpMatchingDataImpl(stream_info_);
  auto matcher_tree = buildMatcherTree(true);

  const auto result = matcher_tree->match(data_);
  // The match was completed, no match found but the invert flag was set.
  EXPECT_EQ(result.match_state_, ::Envoy::Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
}

TEST_F(MetadataMatcherTest, BadData) {
  envoy::extensions::matching::input_matchers::metadata::v3::Metadata meta_matcher;
  meta_matcher.mutable_value()->mutable_string_match()->set_exact(kMetadataValue);
  const auto& matcher_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::matching::input_matchers::metadata::v3::Metadata&>(
      meta_matcher, factory_context_.messageValidationVisitor());
  const auto& v = matcher_config.value();
  auto value_matcher = Envoy::Matchers::ValueMatcher::create(v, factory_context_);

  ::Envoy::Matcher::MatchingDataType data = absl::monostate();
  EXPECT_NO_THROW(Matcher(value_matcher, false).match(data));

  ::Envoy::Matcher::MatchingDataType data2 = std::string("test");
  EXPECT_NO_THROW(Matcher(value_matcher, false).match(data2));
}

} // namespace Metadata
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
