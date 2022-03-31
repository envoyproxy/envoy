#pragma once

#include "source/extensions/filters/network/thrift_proxy/filters/filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace ThriftFilters {

class BidirectionFilterWrapper final : public FilterBase {
public:
  BidirectionFilterWrapper(BidirectionFilterSharedPtr filter);

  // ThriftBaseFilter
  void onDestroy() override { parent_->onDestroy(); }

private:
  BidirectionFilterSharedPtr parent_;
  DecoderFilterSharedPtr decode_filter_;
  EncoderFilterSharedPtr encode_filter_;
};

using BidirectionFilterWrapperSharedPtr = std::shared_ptr<BidirectionFilterWrapper>;

} // namespace ThriftFilters
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
