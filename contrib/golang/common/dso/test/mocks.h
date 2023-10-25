#pragma once

#include <string>

#include "contrib/golang/common/dso/dso.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Dso {

class MockHttpFilterDsoImpl : public HttpFilterDso {
public:
  MockHttpFilterDsoImpl();
  ~MockHttpFilterDsoImpl() override;

  MOCK_METHOD(GoUint64, envoyGoFilterNewHttpPluginConfig, (httpConfig * p0));
  MOCK_METHOD(GoUint64, envoyGoFilterMergeHttpPluginConfig,
              (GoUint64 p0, GoUint64 p1, GoUint64 p2, GoUint64 p3));
  MOCK_METHOD(void, envoyGoFilterDestroyHttpPluginConfig, (GoUint64 p0, GoInt p1));
  MOCK_METHOD(GoUint64, envoyGoFilterOnHttpHeader,
              (httpRequest * p0, GoUint64 p1, GoUint64 p2, GoUint64 p3));
  MOCK_METHOD(GoUint64, envoyGoFilterOnHttpData,
              (httpRequest * p0, GoUint64 p1, GoUint64 p2, GoUint64 p3));
  MOCK_METHOD(void, envoyGoFilterOnHttpLog, (httpRequest * p0, int p1));
  MOCK_METHOD(void, envoyGoFilterOnHttpDestroy, (httpRequest * p0, int p1));
  MOCK_METHOD(void, envoyGoRequestSemaDec, (httpRequest * p0));
};

class MockNetworkFilterDsoImpl : public NetworkFilterDso {
public:
  MockNetworkFilterDsoImpl() = default;
  ~MockNetworkFilterDsoImpl() override = default;

  MOCK_METHOD(GoUint64, envoyGoFilterOnNetworkFilterConfig,
              (GoUint64 libraryIDPtr, GoUint64 libraryIDLen, GoUint64 configPtr,
               GoUint64 configLen));
  MOCK_METHOD(GoUint64, envoyGoFilterOnDownstreamConnection,
              (void* w, GoUint64 pluginNamePtr, GoUint64 pluginNameLen, GoUint64 configID));
  MOCK_METHOD(GoUint64, envoyGoFilterOnDownstreamData,
              (void* w, GoUint64 dataSize, GoUint64 dataPtr, GoInt sliceNum, GoInt endOfStream));
  MOCK_METHOD(void, envoyGoFilterOnDownstreamEvent, (void* w, GoInt event));
  MOCK_METHOD(GoUint64, envoyGoFilterOnDownstreamWrite,
              (void* w, GoUint64 dataSize, GoUint64 dataPtr, GoInt sliceNum, GoInt endOfStream));

  MOCK_METHOD(void, envoyGoFilterOnUpstreamConnectionReady, (void* w, GoUint64 conn_id));
  MOCK_METHOD(void, envoyGoFilterOnUpstreamConnectionFailure,
              (void* w, GoInt reason, GoUint64 conn_id));
  MOCK_METHOD(void, envoyGoFilterOnUpstreamData,
              (void* w, GoUint64 dataSize, GoUint64 dataPtr, GoInt sliceNum, GoInt endOfStream));
  MOCK_METHOD(void, envoyGoFilterOnUpstreamEvent, (void* w, GoInt event));

  MOCK_METHOD(void, envoyGoFilterOnSemaDec, (void* w));
};

} // namespace Dso
} // namespace Envoy
