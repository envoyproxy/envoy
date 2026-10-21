#pragma once

#include "source/extensions/filters/http/tap/tap_config.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

class MockHttpTapConfig : public HttpTapConfig {
public:
  HttpPerRequestTapperPtr
  createPerRequestTapper(const envoy::extensions::filters::http::tap::v3::Tap& tap_config,
                         Http::StreamDecoderFilterCallbacks& decoder_callbacks) override {
    return HttpPerRequestTapperPtr{createPerRequestTapper_(tap_config, decoder_callbacks)};
  }

  Extensions::Common::Tap::PerTapSinkHandleManagerPtr
  createPerTapSinkHandleManager(uint64_t trace_id) override {
    return Extensions::Common::Tap::PerTapSinkHandleManagerPtr{
        createPerTapSinkHandleManager_(trace_id)};
  }

  MOCK_METHOD(HttpPerRequestTapper*, createPerRequestTapper_,
              (const envoy::extensions::filters::http::tap::v3::Tap& tap_config,
               Http::StreamDecoderFilterCallbacks& decoder_callbacks));
  MOCK_METHOD(Extensions::Common::Tap::PerTapSinkHandleManager*, createPerTapSinkHandleManager_,
              (uint64_t trace_id));
  MOCK_METHOD(uint32_t, maxBufferedRxBytes, (), (const, override));
  MOCK_METHOD(uint32_t, maxBufferedTxBytes, (), (const, override));
  MOCK_METHOD(uint32_t, minStreamedSentBytes, (), (const, override));
  MOCK_METHOD(Extensions::Common::Tap::Matcher::MatchStatusVector, createMatchStatusVector, (),
              (const, override));
  MOCK_METHOD(const Extensions::Common::Tap::Matcher&, rootMatcher, (), (const, override));
  MOCK_METHOD(bool, streaming, (), (const, override));
  MOCK_METHOD(TimeSource&, timeSource, (), (const, override));
  MOCK_METHOD(bool, shouldRecord, (), (const, override));
};

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
