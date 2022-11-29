#pragma once

#include <vector>

#include "envoy/common/regex.h"
#include "envoy/extensions/http/early_header_mutation/header_mutation/v3/header_mutation.pb.h"
#include "envoy/extensions/http/early_header_mutation/header_mutation/v3/header_mutation.pb.validate.h"
#include "envoy/http/early_header_mutation.h"

#include "source/common/common/regex.h"
#include "source/common/router/header_parser.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace HeaderMutation {

using HeaderAppendAction = envoy::config::core::v3::HeaderValueOption::HeaderAppendAction;
using HeaderValueOption = envoy::config::core::v3::HeaderValueOption;
using ProtoHeaderMutation =
    envoy::extensions::http::early_header_mutation::header_mutation::v3::HeaderMutation;

class Mutation {
public:
  virtual ~Mutation() = default;

  virtual void mutate(Envoy::Http::RequestHeaderMap& headers,
                      const StreamInfo::StreamInfo& stream_info) const PURE;
};
using MutationPtr = std::unique_ptr<Mutation>;

class HeaderMutation : public Envoy::Http::EarlyHeaderMutation {
public:
  HeaderMutation(const ProtoHeaderMutation& mutations);

  bool mutate(Envoy::Http::RequestHeaderMap& headers,
              const StreamInfo::StreamInfo& stream_info) const override;

private:
  std::vector<MutationPtr> mutations_;
};

} // namespace HeaderMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
