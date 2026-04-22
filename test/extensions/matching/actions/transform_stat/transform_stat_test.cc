#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/stats/symbol_table.h"
#include "source/extensions/matching/actions/transform_stat/transform_stat.h"

#include "test/mocks/protobuf/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace TransformStat {

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

  Stats::SymbolTable symbol_table_;
  Stats::StatNamePool pool_{symbol_table_};
  ActionContext action_context_{pool_};
  testing::NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Matcher::ActionConstSharedPtr action_;
};

TEST_F(TransformStatTest, DropStat) {
  TransformStat config;
  config.mutable_drop_stat();
  createAction(config);

  const auto* stat_action = dynamic_cast<const TransformStatAction*>(action_.get());
  ASSERT_NE(stat_action, nullptr);

  std::string tag_value;
  EXPECT_EQ(TransformStatAction::Result::DropStat, stat_action->apply(tag_value));
}

TEST_F(TransformStatTest, UpdateTag) {
  TransformStat config;
  auto* update_tag = config.mutable_update_tag();
  update_tag->set_new_tag_value("bar");
  createAction(config);

  const auto* stat_action = dynamic_cast<const TransformStatAction*>(action_.get());
  ASSERT_NE(stat_action, nullptr);

  std::string tag_value = "baz";
  EXPECT_EQ(TransformStatAction::Result::Keep, stat_action->apply(tag_value));
  EXPECT_EQ("bar", tag_value);
}

TEST_F(TransformStatTest, DropTag) {
  TransformStat config;
  config.mutable_drop_tag();
  createAction(config);

  const auto* stat_action = dynamic_cast<const TransformStatAction*>(action_.get());
  ASSERT_NE(stat_action, nullptr);

  std::string tag_value = "bar";
  EXPECT_EQ(TransformStatAction::Result::DropTag, stat_action->apply(tag_value));
}

TEST_F(TransformStatTest, EmptyAction) {
  TransformStat config;
  createAction(config);

  const auto* stat_action = dynamic_cast<const TransformStatAction*>(action_.get());
  ASSERT_NE(stat_action, nullptr);

  std::string tag_value;
  EXPECT_EQ(TransformStatAction::Result::Keep, stat_action->apply(tag_value));
}

} // namespace TransformStat
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
