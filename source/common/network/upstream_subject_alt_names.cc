#include "source/common/network/upstream_subject_alt_names.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

auto upstream_subject_alt_names_inline_key =
    InlineMapRegistryHelper::registerInlinKey<StreamInfo::FilterStateInlineMapScope>(
        "envoy.network.upstream_subject_alt_names");

const StreamInfo::InlineKey UpstreamSubjectAltNames::key() {
  return upstream_subject_alt_names_inline_key;
}

} // namespace Network
} // namespace Envoy
