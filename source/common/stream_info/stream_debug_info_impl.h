#pragma once

#include "envoy/common/pure.h"
#include "envoy/stream_info/stream_debug_info.h"

namespace Envoy {
namespace StreamInfo {

/**
 * TODO(mergeconflict): document me
 */
class StreamDebugInfoImpl : public StreamDebugInfo {
public:
  bool getAppendClusterInfo() const override;
  void setAppendClusterInfo(bool value) override;
  bool getDoNotForward() const override;
  void setDoNotForward(bool value) override;

private:
  bool append_cluster_info_{};
  bool do_not_forward_{};
};

} // namespace StreamInfo
} // namespace Envoy