#include "source/extensions/matching/actions/transform_stat/transform_stat.h"

#include "envoy/extensions/matching/actions/transform_stat/v3/transform_stat.pb.validate.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace TransformStat {

namespace {

using ProtoTransformStat = envoy::extensions::matching::actions::transform_stat::v3::TransformStat;

class DropStat : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  explicit DropStat(const ProtoTransformStat::DropStat&) {}

  Result apply(Envoy::Stats::TagVector&) const override { return Result::Drop; }
};

class InsertTag : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  explicit InsertTag(const ProtoTransformStat::InsertTag& config)
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

class DropTag : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  explicit DropTag(const ProtoTransformStat::DropTag& config)
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

class NoOpAction : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  Result apply(Envoy::Stats::TagVector&) const override { return Result::Keep; }
};

} // namespace

class TransformStatActionFactory : public Matcher::ActionFactory<ActionContext> {
public:
  Matcher::ActionConstSharedPtr createAction(
      const Protobuf::Message& config, ActionContext&,
      ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& action_config = MessageUtil::downcastAndValidate<const ProtoTransformStat&>(
        config, validation_visitor);

    if (action_config.has_drop_stat()) {
      return std::make_shared<DropStat>(action_config.drop_stat());
    } else if (action_config.has_drop_tag()) {
      return std::make_shared<DropTag>(action_config.drop_tag());
    } else if (action_config.has_insert_tag()) {
      return std::make_shared<InsertTag>(action_config.insert_tag());
    }

    return std::make_shared<NoOpAction>();
  }

  std::string name() const override {
    return "envoy.extensions.matching.actions.transform_stat.v3.TransformStat";
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoTransformStat>();
  }
};

REGISTER_FACTORY(TransformStatActionFactory, Matcher::ActionFactory<ActionContext>);

} // namespace TransformStat
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
