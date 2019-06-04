#include "common/protobuf/utility.h"

#include "extensions/common/tap/tap.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace envoy {
namespace data {
namespace tap {
namespace v2alpha {

// TODO(mattklein123): AFAICT gtest has built in printing for proto messages but it doesn't seem
// to work unless this is here.
std::ostream& operator<<(std::ostream& os, const TraceWrapper& trace);

} // namespace v2alpha
} // namespace tap
} // namespace data
} // namespace envoy

namespace Envoy {
namespace Extensions {

// TODO(mattklein123): Make this a common matcher called ProtoYamlEq and figure out how to
// correctly templatize it.
MATCHER_P(TraceEqual, rhs, "") {
  envoy::data::tap::v2alpha::TraceWrapper expected_trace;
  TestUtility::loadFromYaml(rhs, expected_trace);
  return TestUtility::protoEqual(expected_trace, arg);
}

namespace Common {
namespace Tap {

class MockPerTapSinkHandleManager : public PerTapSinkHandleManager {
public:
  MockPerTapSinkHandleManager();
  ~MockPerTapSinkHandleManager();

  void submitTrace(TraceWrapperPtr&& trace) override { submitTrace_(*trace); }

  MOCK_METHOD1(submitTrace_, void(const envoy::data::tap::v2alpha::TraceWrapper& trace));
};

class MockMatcher : public Matcher {
public:
  using Matcher::Matcher;
  ~MockMatcher();

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
