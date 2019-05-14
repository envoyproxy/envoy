#include "common/stream_info/stream_debug_info_impl.h"

namespace Envoy {
namespace StreamInfo {

bool StreamDebugInfoImpl::getAppendClusterInfo() const { return append_cluster_info_; }
void StreamDebugInfoImpl::setAppendClusterInfo(bool value) { append_cluster_info_ = value; }
bool StreamDebugInfoImpl::getDoNotForward() const { return do_not_forward_; }
void StreamDebugInfoImpl::setDoNotForward(bool value) { do_not_forward_ = value; }

} // namespace StreamInfo
} // namespace Envoy