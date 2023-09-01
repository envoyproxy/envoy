#include "source/common/network/upstream_subject_alt_names.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

constexpr absl::string_view UpstreamSubjectAltNamesKey = "envoy.network.upstream_subject_alt_names";

REGISTER_INLINE_MAP_KEY(StreamInfo::FilterStateInlineMapScope, UpstreamSubjectAltNamesKey);

const StreamInfo::InlineKey UpstreamSubjectAltNames::key() {
  INLINE_HANDLE_BY_KEY_ON_FIRST_USE(StreamInfo::FilterStateInlineMapScope,
                                    UpstreamSubjectAltNamesKey);
}

} // namespace Network
} // namespace Envoy
