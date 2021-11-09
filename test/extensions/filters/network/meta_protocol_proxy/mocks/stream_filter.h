#pragma once

#include "source/extensions/filters/network/meta_protocol_proxy/interface/filter.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

class MockStreamFilter : public StreamFilter {
public:
  MockStreamFilter();

  MOCK_METHOD(void, onDestroy, ());

  MOCK_METHOD(void, setEncoderFilterCallbacks, (EncoderFilterCallback & callbacks));
  MOCK_METHOD(FilterStatus, onStreamEncoded, (Response & response));

  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallback & callbacks));
  MOCK_METHOD(FilterStatus, onStreamDecoded, (Request & response));
};

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
