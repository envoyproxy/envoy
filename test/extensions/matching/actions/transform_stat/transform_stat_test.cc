#include "envoy/extensions/matching/actions/transform_stat/v3/transform_stat.pb.h"
#include "envoy/registry/registry.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "source/common/config/utility.h"
#include "source/extensions/matching/actions/transform_stat/transform_stat.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace TransformStat {
namespace {

using ::envoy::extensions::matching::actions::transform_stat::v3::TransformStat;

class TransformStatTest : public testing::Test {
public:
  TransformStatTest() = default;

  void createAction(const TransformStat& config) {
    auto& factory =
        Config::Utility::getAndCheckFactoryByName<Matcher::ActionFactory<ActionContext>>(
            "envoy.extensions.matching.actions.transform_stat.v3.TransformStat");
    action_ = factory.createAction(config, action_context_, validation_visitor_);
  }

  Matcher::ActionConstSharedPtr action_;
  ActionContext action_context_;
  testing::NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
};

TEST_F(TransformStatTest, DropStat) {
  TransformStat config;
  config.mutable_drop_stat();

  createAction(config);

  const auto* stat_action = dynamic_cast<const TransformStatAction*>(action_.get());
  ASSERT_NE(stat_action, nullptr);

  Envoy::Stats::TagVector tags;
  EXPECT_EQ(TransformStatAction::Result::Drop, stat_action->apply(tags));
}

TEST_F(TransformStatTest, InsertTag) {
  TransformStat config;
  auto* insert_tag = config.mutable_insert_tag();
  insert_tag->set_tag_name("foo");
  insert_tag->set_tag_value("bar");

  createAction(config);

  const auto* stat_action = dynamic_cast<const TransformStatAction*>(action_.get());
  ASSERT_NE(stat_action, nullptr);

  // Case 1: Tag does not exist
  Envoy::Stats::TagVector tags;
  EXPECT_EQ(TransformStatAction::Result::Keep, stat_action->apply(tags));
  ASSERT_EQ(1, tags.size());
  EXPECT_EQ("foo", tags[0].name_);
  EXPECT_EQ("bar", tags[0].value_);

  // Case 2: Tag exists and should be updated
  tags.clear();
  tags.emplace_back(Envoy::Stats::Tag{"foo", "baz"});
  EXPECT_EQ(TransformStatAction::Result::Keep, stat_action->apply(tags));
  ASSERT_EQ(1, tags.size());
  EXPECT_EQ("foo", tags[0].name_);
  EXPECT_EQ("bar", tags[0].value_);

  // Case 3: Other tags exist
  tags.clear();
  tags.emplace_back(Envoy::Stats::Tag{"other", "value"});
  EXPECT_EQ(TransformStatAction::Result::Keep, stat_action->apply(tags));
  ASSERT_EQ(2, tags.size());
  EXPECT_EQ("other", tags[0].name_);
  EXPECT_EQ("foo", tags[1].name_);
  EXPECT_EQ("bar", tags[1].value_);
}

TEST_F(TransformStatTest, DropTag) {
  TransformStat config;
  auto* drop_tag = config.mutable_drop_tag();
  drop_tag->set_target_tag_name("foo");

  createAction(config);

  const auto* stat_action = dynamic_cast<const TransformStatAction*>(action_.get());
  ASSERT_NE(stat_action, nullptr);

  // Case 1: Tag exists and should be dropped
  Envoy::Stats::TagVector tags;
  tags.emplace_back(Envoy::Stats::Tag{"foo", "bar"});
  EXPECT_EQ(TransformStatAction::Result::Keep, stat_action->apply(tags));
  EXPECT_TRUE(tags.empty());

  // Case 2: Tag does not exist
  tags.clear();
  tags.emplace_back(Envoy::Stats::Tag{"other", "value"});
  EXPECT_EQ(TransformStatAction::Result::Keep, stat_action->apply(tags));
  ASSERT_EQ(1, tags.size());
  EXPECT_EQ("other", tags[0].name_);

  // Case 3: Multiple tags
  tags.clear();
  tags.emplace_back(Envoy::Stats::Tag{"other", "value"});
  tags.emplace_back(Envoy::Stats::Tag{"foo", "bar"});
  EXPECT_EQ(TransformStatAction::Result::Keep, stat_action->apply(tags));
  ASSERT_EQ(1, tags.size());
  EXPECT_EQ("other", tags[0].name_);
}

TEST_F(TransformStatTest, EmptyAction) {
  TransformStat config;
  createAction(config);

  const auto* stat_action = dynamic_cast<const TransformStatAction*>(action_.get());
  ASSERT_NE(stat_action, nullptr);

  Envoy::Stats::TagVector tags;
  EXPECT_EQ(TransformStatAction::Result::Keep, stat_action->apply(tags));
}

TEST_F(TransformStatTest, CombinedAction) {
  TransformStat config;
  auto* drop_tag = config.mutable_drop_tag();
  drop_tag->set_target_tag_name("foo");
  auto* insert_tag = config.mutable_insert_tag();
  insert_tag->set_tag_name("bar");
  insert_tag->set_tag_value("baz");

  createAction(config);

  const auto* stat_action = dynamic_cast<const TransformStatAction*>(action_.get());
  ASSERT_NE(stat_action, nullptr);

  // Input: [foo=1, other=2]
  // Expected Output: [other=2] (insert_tag is ignored due to precedence)
  Envoy::Stats::TagVector tags;
  tags.emplace_back(Envoy::Stats::Tag{"foo", "1"});
  tags.emplace_back(Envoy::Stats::Tag{"other", "2"});

  EXPECT_EQ(TransformStatAction::Result::Keep, stat_action->apply(tags));
  ASSERT_EQ(1, tags.size());
  // drop_tag removed "foo".
  // insert_tag is skipped.
  // "other" remains.

  EXPECT_EQ("other", tags[0].name_);
  EXPECT_EQ("2", tags[0].value_);
}

TEST_F(TransformStatTest, CombinedDropStat) {
  TransformStat config;
  config.mutable_drop_stat();
  auto* insert_tag = config.mutable_insert_tag();
  insert_tag->set_tag_name("bar");
  insert_tag->set_tag_value("baz");

  createAction(config);
  const auto* stat_action = dynamic_cast<const TransformStatAction*>(action_.get());
  ASSERT_NE(stat_action, nullptr);

  Envoy::Stats::TagVector tags;
  EXPECT_EQ(TransformStatAction::Result::Drop, stat_action->apply(tags));
}

} // namespace
} // namespace TransformStat
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
