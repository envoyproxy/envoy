#include "envoy/extensions/matching/actions/transform_stat/v3/transform_stat.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/matching/actions/transform_stat/transform_stat.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace TransformStat {

class TransformStatActionFactory : public Matcher::ActionFactory<ActionContext> {
public:
  Matcher::ActionConstSharedPtr
  createAction(const Protobuf::Message& config, ActionContext& context,
               ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& action_config =
        MessageUtil::downcastAndValidate<const ProtoTransformStat&>(config, validation_visitor);

    if (action_config.has_drop_stat()) {
      return std::make_shared<DropStat>(action_config.drop_stat());
    } else if (action_config.has_drop_tag()) {
      const auto& drop_tag = action_config.drop_tag();
      return std::make_shared<DropTag>(context.pool_.add(drop_tag.target_tag_name()));
    } else if (action_config.has_upsert_tag()) {
      const auto& upsert_tag = action_config.upsert_tag();
      return std::make_shared<UpsertTag>(context.pool_.add(upsert_tag.tag_name()),
                                         context.pool_.add(upsert_tag.tag_value()));
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
