#pragma once

#include "envoy/server/admin.h"

#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {

/**
 * MockAdminStream mocks AdminStream while returning a NiceMock for
 * getDecoderFilterCallbacks. This class is useful in situations where
 * you do not need to enforce calls to different decoder filter callbacks.
 */
class MockAdminStream : public AdminStream {
public:
  MockAdminStream();
  ~MockAdminStream() override;

  MOCK_METHOD(void, setEndStreamOnComplete, (bool), (override));
  MOCK_METHOD(void, addOnDestroyCallback, (std::function<void()>), (override));
  MOCK_METHOD(const Buffer::Instance*, getRequestBody, (), (const, override));
  MOCK_METHOD(Http::RequestHeaderMap&, getRequestHeaders, (), (const, override));
  MOCK_METHOD(NiceMock<Http::MockStreamDecoderFilterCallbacks>&, getDecoderFilterCallbacks, (),
              (const, override));
  MOCK_METHOD(Http::Http1StreamEncoderOptionsOptRef, http1StreamEncoderOptions, (), (override));
  MOCK_METHOD(Http::Utility::QueryParamsMulti, queryParams, (), (const, override));
};

/**
 * StrictMockAdminStream mocks AdminStream while returning a StrictMock
 * for getDecoderFilterCallbacks. This class is useful in situations where
 * you want to enforce calls on the decoder filter callbacks.
 */
class StrictMockAdminStream : public AdminStream {
public:
  StrictMockAdminStream();
  ~StrictMockAdminStream() override;

  MOCK_METHOD(void, setEndStreamOnComplete, (bool), (override));
  MOCK_METHOD(void, addOnDestroyCallback, (std::function<void()>), (override));
  MOCK_METHOD(const Buffer::Instance*, getRequestBody, (), (const, override));
  MOCK_METHOD(Http::RequestHeaderMap&, getRequestHeaders, (), (const, override));
  MOCK_METHOD(::testing::StrictMock<Http::MockStreamDecoderFilterCallbacks>&,
              getDecoderFilterCallbacks, (), (const, override));
  MOCK_METHOD(Http::Http1StreamEncoderOptionsOptRef, http1StreamEncoderOptions, (), (override));
  MOCK_METHOD(Http::Utility::QueryParamsMulti, queryParams, (), (const, override));
};
} // namespace Server
} // namespace Envoy
