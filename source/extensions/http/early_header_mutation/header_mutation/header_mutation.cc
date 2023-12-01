#include "source/extensions/http/early_header_mutation/header_mutation/header_mutation.h"

#include "envoy/config/common/mutation_rules/v3/mutation_rules.pb.h"

#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace HeaderMutation {

HeaderMutation::HeaderMutation(const ProtoHeaderMutation& mutations)
    : mutations_(mutations.mutations()) {}

bool HeaderMutation::mutate(Envoy::Http::RequestHeaderMap& headers,
                            const StreamInfo::StreamInfo& stream_info) const {
  mutations_.evaluateHeaders(headers, {&headers}, stream_info);
  return true;
}

} // namespace HeaderMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
