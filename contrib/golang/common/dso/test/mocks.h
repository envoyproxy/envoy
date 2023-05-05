#include <string>

#include "contrib/golang/common/dso/dso.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Dso {

class MockHttpFilterDsoImpl : public HttpFilterDso {
public:
  MockHttpFilterDsoImpl();
  ~MockHttpFilterDsoImpl() override;

  MOCK_METHOD(GoUint64, envoyGoFilterNewHttpPluginConfig, (GoUint64 p0, GoUint64 p1));
  MOCK_METHOD(GoUint64, envoyGoFilterMergeHttpPluginConfig, (GoUint64 p0, GoUint64 p1));
  MOCK_METHOD(GoUint64, envoyGoFilterOnHttpHeader,
              (httpRequest * p0, GoUint64 p1, GoUint64 p2, GoUint64 p3));
  MOCK_METHOD(GoUint64, envoyGoFilterOnHttpData,
              (httpRequest * p0, GoUint64 p1, GoUint64 p2, GoUint64 p3));
  MOCK_METHOD(void, envoyGoFilterOnHttpDestroy, (httpRequest * p0, int p1));
};

} // namespace Dso
} // namespace Envoy
