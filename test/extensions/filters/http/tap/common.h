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

  MOCK_METHOD1(createPerRequestTapper_, HttpPerRequestTapper*(uint64_t stream_id));
  MOCK_METHOD1(createPerTapSinkHandleManager_,
               Extensions::Common::Tap::PerTapSinkHandleManager*(uint64_t trace_id));
  MOCK_CONST_METHOD0(maxBufferedRxBytes, uint32_t());
  MOCK_CONST_METHOD0(maxBufferedTxBytes, uint32_t());
  MOCK_CONST_METHOD0(createMatchStatusVector,
                     Extensions::Common::Tap::Matcher::MatchStatusVector());
  MOCK_CONST_METHOD0(rootMatcher, const Extensions::Common::Tap::Matcher&());
  MOCK_CONST_METHOD0(streaming, bool());
};

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
