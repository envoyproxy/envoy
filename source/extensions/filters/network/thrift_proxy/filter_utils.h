#pragma once

#include "source/extensions/filters/network/thrift_proxy/filters/filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace ThriftFilters {

class BidirectionalFilterWrapper final : public FilterBase {
public:
  BidirectionalFilterWrapper(BidirectionalFilterSharedPtr filter);

  // ThriftBaseFilter
  void onDestroy() override { parent_->onDestroy(); }

  DecoderFilterSharedPtr decoder_filter_;
  EncoderFilterSharedPtr encoder_filter_;

private:
  BidirectionalFilterSharedPtr parent_;
};

using BidirectionalFilterWrapperSharedPtr = std::shared_ptr<BidirectionalFilterWrapper>;

} // namespace ThriftFilters
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
