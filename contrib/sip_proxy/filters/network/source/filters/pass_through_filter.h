#pragma once

#include "contrib/sip_proxy/filters/network/source/filters/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace SipFilters {

/**
 * Pass through Sip decoder filter. Continue at each decoding state within the series of
 * transitions.
 */
class PassThroughDecoderFilter : public DecoderFilter {
public:
  // SipDecoderFilter
  void onDestroy() override {}

  void setDecoderFilterCallbacks(DecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  };

  // Sip Decoder State Machine
  SipProxy::FilterStatus transportBegin(SipProxy::MessageMetadataSharedPtr) override {
    return SipProxy::FilterStatus::Continue;
  }

  SipProxy::FilterStatus transportEnd() override { return SipProxy::FilterStatus::Continue; }

  SipProxy::FilterStatus messageBegin(SipProxy::MessageMetadataSharedPtr) override {
    return SipProxy::FilterStatus::Continue;
  }

  SipProxy::FilterStatus messageEnd() override { return SipProxy::FilterStatus::Continue; }

protected:
  DecoderFilterCallbacks* decoder_callbacks_{};
};

} // namespace SipFilters
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
