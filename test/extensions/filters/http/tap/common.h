#include "extensions/filters/http/tap/tap_config.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

class MockHttpTapConfig : public HttpTapConfig {
public:
  HttpPerRequestTapperPtr createPerRequestTapper(uint64_t stream_id) override {
    return HttpPerRequestTapperPtr{createPerRequestTapper_(stream_id)};
  }

  Extensions::Common::Tap::PerTapSinkHandleManagerPtr
  createPerTapSinkHandleManager(uint64_t trace_id) override {
    return Extensions::Common::Tap::PerTapSinkHandleManagerPtr{
        createPerTapSinkHandleManager_(trace_id)};
  }

  MOCK_METHOD(HttpPerRequestTapper*, createPerRequestTapper_, (uint64_t stream_id));
  MOCK_METHOD(Extensions::Common::Tap::PerTapSinkHandleManager*, createPerTapSinkHandleManager_,
              (uint64_t trace_id));
  MOCK_METHOD(uint32_t, maxBufferedRxBytes, (), (const));
  MOCK_METHOD(uint32_t, maxBufferedTxBytes, (), (const));
  MOCK_METHOD(Extensions::Common::Tap::Matcher::MatchStatusVector, createMatchStatusVector, (),
              (const));
  MOCK_METHOD(const Extensions::Common::Tap::Matcher&, rootMatcher, (), (const));
  MOCK_METHOD(bool, streaming, (), (const));
};

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
