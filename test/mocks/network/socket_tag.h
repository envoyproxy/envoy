#pragma once

#include "envoy/network/socket_tag.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Network {

class MockSocketTag : public SocketTag {
public:
  MockSocketTag() = default;
  ~MockSocketTag() override = default;

  MOCK_METHOD(void, apply, (IoHandle& io_handle), (const override));
  MOCK_METHOD(void, hashKey, (std::vector<uint8_t>& key), (const override));
  MOCK_METHOD(std::string, dataForLogging, (), (const override));
};

} // namespace Network
} // namespace Envoy
