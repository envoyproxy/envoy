#pragma once

#include "envoy/config/common/mutation_rules/v3/mutation_rules.pb.h"
#include "envoy/http/header_evaluator.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Http {

using ProtoHeaderMutatons =
    Protobuf::RepeatedPtrField<envoy::config::common::mutation_rules::v3::HeaderMutation>;
using ProtoHeaderValueOption = envoy::config::core::v3::HeaderValueOption;

class HeaderMutations : public HeaderEvaluator {
public:
  HeaderMutations(const ProtoHeaderMutatons& header_mutations);

  // Http::HeaderEvaluator
  void evaluateHeaders(Http::HeaderMap& headers, const Formatter::HttpFormatterContext& context,
                       const StreamInfo::StreamInfo& stream_info) const override;

private:
  std::vector<std::unique_ptr<HeaderEvaluator>> header_mutations_;
};

} // namespace Http
} // namespace Envoy
