#pragma once

#include "envoy/server/overload/reset_streams_adapter.h"

namespace Envoy {
namespace Server {

class ResetStreamAdapterImpl : public ResetStreamAdapter {
public:
  ResetStreamAdapterImpl(double lower_limit, double upper_limit);

  // ResetStreamAdapter
  absl::optional<uint32_t> translateToBucketsToReset(OverloadActionState state) const override;

private:
  double lower_limit_;
  double upper_limit_;
  double bucket_gradation_;
};

} // namespace Server
} // namespace Envoy
