#include "common/protobuf/utility.h"

#include "extensions/common/tap/tap.h"

#include "gmock/gmock.h"

namespace envoy {
namespace data {
namespace tap {
namespace v2alpha {
// TODO(mattklein123): AFAICT gtest has built in printing for proto messages but it doesn't seem
// to work unless this is here.
std::ostream& operator<<(std::ostream& os, const TraceWrapper& trace) {
  return os << Envoy::MessageUtil::getJsonStringFromMessage(trace, true, false);
}
} // namespace v2alpha
} // namespace tap
} // namespace data
} // namespace envoy

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

class MockPerTapSinkHandleManager : public PerTapSinkHandleManager {
public:
  void submitTrace(const TraceWrapperSharedPtr& trace) override { submitTrace_(*trace); }

  MOCK_METHOD1(submitTrace_, void(const envoy::data::tap::v2alpha::TraceWrapper& trace));
};

class MockMatcher : public Matcher {
public:
  using Matcher::Matcher;

  MOCK_CONST_METHOD1(onNewStream, void(MatchStatusVector& statuses));
  MOCK_CONST_METHOD2(onHttpRequestHeaders,
                     void(const Http::HeaderMap& request_headers, MatchStatusVector& statuses));
  MOCK_CONST_METHOD2(onHttpRequestTrailers,
                     void(const Http::HeaderMap& request_trailers, MatchStatusVector& statuses));
  MOCK_CONST_METHOD2(onHttpResponseHeaders,
                     void(const Http::HeaderMap& response_headers, MatchStatusVector& statuses));
  MOCK_CONST_METHOD2(onHttpResponseTrailers,
                     void(const Http::HeaderMap& response_trailers, MatchStatusVector& statuses));
};

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
