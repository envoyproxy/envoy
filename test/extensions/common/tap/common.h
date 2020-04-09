#include "envoy/data/tap/v3/wrapper.pb.h"

#include "common/protobuf/utility.h"

#include "extensions/common/tap/tap.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace envoy {
namespace data {
namespace tap {
namespace v3 {

// TODO(mattklein123): AFAICT gtest has built in printing for proto messages but it doesn't seem
// to work unless this is here.
std::ostream& operator<<(std::ostream& os, const TraceWrapper& trace);

} // namespace v3
} // namespace tap
} // namespace data
} // namespace envoy

namespace Envoy {
namespace Extensions {

// TODO(mattklein123): Make this a common matcher called ProtoYamlEq and figure out how to
// correctly templatize it.
MATCHER_P(TraceEqual, rhs, "") {
  envoy::data::tap::v3::TraceWrapper expected_trace;
  TestUtility::loadFromYaml(rhs, expected_trace);
  return TestUtility::protoEqual(expected_trace, arg);
}

namespace Common {
namespace Tap {

class MockPerTapSinkHandleManager : public PerTapSinkHandleManager {
public:
  MockPerTapSinkHandleManager();
  ~MockPerTapSinkHandleManager() override;

  void submitTrace(TraceWrapperPtr&& trace) override { submitTrace_(*trace); }

  MOCK_METHOD(void, submitTrace_, (const envoy::data::tap::v3::TraceWrapper& trace));
};

class MockMatcher : public Matcher {
public:
  using Matcher::Matcher;
  ~MockMatcher() override;

  MOCK_METHOD(void, onNewStream, (MatchStatusVector & statuses), (const));
  MOCK_METHOD(void, onHttpRequestHeaders,
              (const Http::RequestHeaderMap& request_headers, MatchStatusVector& statuses),
              (const));
  MOCK_METHOD(void, onHttpRequestTrailers,
              (const Http::RequestTrailerMap& request_trailers, MatchStatusVector& statuses),
              (const));
  MOCK_METHOD(void, onHttpResponseHeaders,
              (const Http::ResponseHeaderMap& response_headers, MatchStatusVector& statuses),
              (const));
  MOCK_METHOD(void, onHttpResponseTrailers,
              (const Http::ResponseTrailerMap& response_trailers, MatchStatusVector& statuses),
              (const));
};

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
