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
      return std::make_shared<DropStat>(action_config.drop_stat(), context.symbol_table_);
    } else if (action_config.has_drop_tag()) {
      return std::make_shared<DropTag>(action_config.drop_tag(), context.symbol_table_);
    } else if (action_config.has_insert_tag()) {
      return std::make_shared<InsertTag>(action_config.insert_tag(), context.symbol_table_);
    }

    return std::make_shared<NoOpAction>(context.symbol_table_);
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
