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

using ProtoHeaderMutation =
    envoy::extensions::http::early_header_mutation::header_mutation::v3::HeaderMutation;

class HeaderMutation : public Envoy::Http::EarlyHeaderMutation {
public:
  HeaderMutation(const ProtoHeaderMutation& mutations) {
    header_parser_ = Router::HeaderParser::configure(mutations.headers_to_append(),
                                                     mutations.headers_to_remove());
  }
  bool mutate(Envoy::Http::RequestHeaderMap& headers,
              const StreamInfo::StreamInfo& stream_info) const override;

private:
  Router::HeaderParserPtr header_parser_;
};

} // namespace HeaderMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
