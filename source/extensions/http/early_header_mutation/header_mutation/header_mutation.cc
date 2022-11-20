#include "source/extensions/http/early_header_mutation/header_mutation/header_mutation.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace HeaderMutation {

bool HeaderMutation::mutate(Envoy::Http::RequestHeaderMap& headers,
                            const StreamInfo::StreamInfo& stream_info) const {
  header_parser_->evaluateHeaders(headers, headers,
                                  *Envoy::Http::StaticEmptyHeaders::get().response_headers.get(),
                                  stream_info);
  return true;
}

} // namespace HeaderMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
