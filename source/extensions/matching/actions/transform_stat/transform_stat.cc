#include "source/extensions/matching/actions/transform_stat/transform_stat.h"

#include "envoy/extensions/matching/actions/transform_stat/v3/transform_stat.pb.validate.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace TransformStat {

namespace {

class DropStat : public TransformStatAction {
public:
  explicit DropStat(
      const envoy::extensions::matching::actions::transform_stat::v3::TransformStat::
          DropStat&) {}

  Result apply(Envoy::Stats::TagVector&) const override { return Result::Drop; }
};

class InsertTag : public TransformStatAction {
public:
  explicit InsertTag(
      const envoy::extensions::matching::actions::transform_stat::v3::TransformStat::
          InsertTag& config)
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

class DropTag : public TransformStatAction {
public:
  explicit DropTag(
      const envoy::extensions::matching::actions::transform_stat::v3::TransformStat::
          DropTag& config)
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

class CompositeTransformStatAction
    : public Matcher::ActionBase<
          envoy::extensions::matching::actions::transform_stat::v3::TransformStat>,
      public TransformStatAction {
public:
  CompositeTransformStatAction(std::vector<std::unique_ptr<TransformStatAction>> actions)
      : actions_(std::move(actions)) {}

  Result apply(Envoy::Stats::TagVector& tags) const override {
    for (const auto& action : actions_) {
      if (action->apply(tags) == Result::Drop) {
        return Result::Drop;
      }
    }
    return Result::Keep;
  }

private:
  const std::vector<std::unique_ptr<TransformStatAction>> actions_;
};

} // namespace

class TransformStatActionFactory : public Matcher::ActionFactory<ActionContext> {
public:
  Matcher::ActionConstSharedPtr createAction(
      const Protobuf::Message& config, ActionContext&,
      ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& action_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::matching::actions::transform_stat::v3::TransformStat&>(
        config, validation_visitor);

    std::vector<std::unique_ptr<TransformStatAction>> actions;

    if (action_config.has_drop_stat()) {
      actions.push_back(
          std::make_unique<DropStat>(action_config.drop_stat()));
    } else if (action_config.has_drop_tag()) {
      actions.push_back(
          std::make_unique<DropTag>(action_config.drop_tag()));
    } else if (action_config.has_insert_tag()) {
      actions.push_back(
          std::make_unique<InsertTag>(action_config.insert_tag()));
    }

    return std::make_shared<CompositeTransformStatAction>(std::move(actions));
  }

  std::string name() const override {
    return "envoy.extensions.matching.actions.transform_stat.v3.TransformStat";
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::actions::transform_stat::v3::TransformStat>();
  }
};

REGISTER_FACTORY(TransformStatActionFactory, Matcher::ActionFactory<ActionContext>);

} // namespace TransformStat
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
