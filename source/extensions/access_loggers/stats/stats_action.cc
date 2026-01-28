#include "source/extensions/access_loggers/stats/stats_action.h"

#include "envoy/extensions/access_loggers/stats/v3/stats.pb.validate.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

class DropStatActionFactory : public Matcher::ActionFactory<ActionContext> {
public:
  Matcher::ActionConstSharedPtr
  createAction(const Protobuf::Message& config, ActionContext&,
               ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& action_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::access_loggers::stats::v3::DropStatAction&>(config,
                                                                             validation_visitor);
    return std::make_shared<DropStatAction>(action_config);
  }

  std::string name() const override {
    return "envoy.extensions.access_loggers.stats.v3.DropStatAction";
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::access_loggers::stats::v3::DropStatAction>();
  }
};

REGISTER_FACTORY(DropStatActionFactory, Matcher::ActionFactory<ActionContext>);

class InsertTagActionFactory : public Matcher::ActionFactory<ActionContext> {
public:
  Matcher::ActionConstSharedPtr
  createAction(const Protobuf::Message& config, ActionContext&,
               ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& action_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::access_loggers::stats::v3::InsertTagAction&>(config,
                                                                              validation_visitor);
    return std::make_shared<InsertTagAction>(action_config);
  }

  std::string name() const override {
    return "envoy.extensions.access_loggers.stats.v3.InsertTagAction";
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::access_loggers::stats::v3::InsertTagAction>();
  }
};

class DropTagActionFactory : public Matcher::ActionFactory<ActionContext> {
public:
  Matcher::ActionConstSharedPtr
  createAction(const Protobuf::Message& config, ActionContext&,
               ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& action_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::access_loggers::stats::v3::DropTagAction&>(config,
                                                                            validation_visitor);
    return std::make_shared<DropTagAction>(action_config);
  }

  std::string name() const override {
    return "envoy.extensions.access_loggers.stats.v3.DropTagAction";
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::access_loggers::stats::v3::DropTagAction>();
  }
};

REGISTER_FACTORY(InsertTagActionFactory, Matcher::ActionFactory<ActionContext>);
REGISTER_FACTORY(DropTagActionFactory, Matcher::ActionFactory<ActionContext>);

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
