#pragma once

#include "envoy/extensions/path/match/pattern_template/v3/pattern_template_match.pb.h"
#include "envoy/extensions/path/match/pattern_template/v3/pattern_template_match.pb.validate.h"
#include "envoy/router/path_match_policy.h"

#include "source/extensions/path/match/pattern_template/pattern_template_match.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Match {

class PatternTemplateMatchPredicateFactory : public Router::PathMatchPredicateFactory {
public:
  Router::PathMatchPredicateSharedPtr
  createPathMatchPredicate(const Protobuf::Message& config) override {
    //TODO: Get pattern from config
    auto path_match_config =
        MessageUtil::downcastAndValidate<const envoy::extensions::path::
                                             match::pattern_template::v3::PatternTemplateMatchConfig&>(
            config, ProtobufMessage::getStrictValidationVisitor());
    return std::make_shared<PatternTemplateMatchPredicate>(path_match_config);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::path::match::pattern_template::v3::PatternTemplateMatchConfig>();
  }

  std::string name() const override { return "envoy.path.match.pattern_template.v3.pattern_template_match_predicate"; }
  std::string category() const override { return "envoy.path.match"; }
};

} // namespace Match
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
