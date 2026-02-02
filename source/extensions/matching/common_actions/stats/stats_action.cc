#include "source/extensions/matching/common_actions/stats/stats_action.h"

#include "envoy/extensions/matching/common_actions/stats/v3/actions.pb.validate.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace CommonActions {
namespace Stats {

namespace {

class DropStatAction
    : public Matcher::ActionBase<
          envoy::extensions::matching::common_actions::stats::v3::StatAction::DropStatAction>,
      public StatsAction {
public:
  explicit DropStatAction(
      const envoy::extensions::matching::common_actions::stats::v3::StatAction::
          DropStatAction&) {}

  Result apply(Envoy::Stats::TagVector&) const override {
    return Result::Drop;
  }
};

class InsertTagAction
    : public Matcher::ActionBase<
          envoy::extensions::matching::common_actions::stats::v3::StatAction::
              InsertTagAction>,
      public StatsAction {
public:
  explicit InsertTagAction(
      const envoy::extensions::matching::common_actions::stats::v3::StatAction::
          InsertTagAction& config)
      : tag_name_(config.tag_name()), tag_value_(config.tag_value()) {}

  Result apply(Envoy::Stats::TagVector& tags) const override {
    bool replaced = false;
    for (auto& tag : tags) {
      if (tag.name_ == tag_name_) {
        tag.value_ = tag_value_;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      tags.emplace_back(tag_name_, tag_value_);
    }
    return Result::Keep;
  }

private:
  const std::string tag_name_;
  const std::string tag_value_;
};

class DropTagAction
    : public Matcher::ActionBase<
          envoy::extensions::matching::common_actions::stats::v3::StatAction::DropTagAction>,
      public StatsAction {
public:
  explicit DropTagAction(
      const envoy::extensions::matching::common_actions::stats::v3::StatAction::
          DropTagAction& config)
      : target_tag_name_(config.target_tag_name()) {}

  Result apply(Envoy::Stats::TagVector& tags) const override {
    for (auto it = tags.begin(); it != tags.end();) {
      if (it->name_ == target_tag_name_) {
        it = tags.erase(it);
      } else {
        ++it;
      }
    }
    return Result::Keep;
  }

private:
  const std::string target_tag_name_;
};

} // namespace

class StatActionFactory : public Matcher::ActionFactory<ActionContext> {
public:
  Matcher::ActionConstSharedPtr createAction(
      const Protobuf::Message& config, ActionContext&,
      ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& action_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::matching::common_actions::stats::v3::StatAction&>(
        config, validation_visitor);

    switch (action_config.rule_case()) {
    case envoy::extensions::matching::common_actions::stats::v3::StatAction::RuleCase::
        kDropStat:
      return std::make_shared<DropStatAction>(action_config.drop_stat());
    case envoy::extensions::matching::common_actions::stats::v3::StatAction::RuleCase::
        kDropTag:
      return std::make_shared<DropTagAction>(action_config.drop_tag());
    case envoy::extensions::matching::common_actions::stats::v3::StatAction::RuleCase::
        kInsertTag:
      return std::make_shared<InsertTagAction>(action_config.insert_tag());
    case envoy::extensions::matching::common_actions::stats::v3::StatAction::RuleCase::
        RULE_NOT_SET:
      break;
    }

    throw EnvoyException(fmt::format("Unknown state action: {}",
                                     action_config.DebugString()));
  }

  std::string name() const override {
    return "envoy.extensions.matching.common_actions.stats.v3.StatAction";
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_actions::stats::v3::StatAction>();
  }
};

REGISTER_FACTORY(StatActionFactory, Matcher::ActionFactory<ActionContext>);

} // namespace Stats
} // namespace CommonActions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
