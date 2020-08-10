#pragma once

#include "envoy/server/admin.h"

#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
class MockAdminStream : public AdminStream {
public:
  MockAdminStream();
  ~MockAdminStream() override;

  MOCK_METHOD(void, setEndStreamOnComplete, (bool));
  MOCK_METHOD(void, addOnDestroyCallback, (std::function<void()>));
  MOCK_METHOD(const Buffer::Instance*, getRequestBody, (), (const));
  MOCK_METHOD(Http::RequestHeaderMap&, getRequestHeaders, (), (const));
  MOCK_METHOD(NiceMock<Http::MockStreamDecoderFilterCallbacks>&, getDecoderFilterCallbacks, (),
              (const));
  MOCK_METHOD(Http::Http1StreamEncoderOptionsOptRef, http1StreamEncoderOptions, ());
};
} // namespace Server
} // namespace Envoy
