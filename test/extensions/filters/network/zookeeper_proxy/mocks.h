#pragma once

#include <cstdint>
#include <string>

#include "source/extensions/filters/network/zookeeper_proxy/decoder.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

class MockDecoderCallbacks : public DecoderCallbacks {
public:
  MockDecoderCallbacks();
  ~MockDecoderCallbacks() override;

  MOCK_METHOD(void, onDecodeError, ());
  MOCK_METHOD(void, onRequestBytes, (uint64_t bytes));
  MOCK_METHOD(void, onConnect, (bool readonly));
  MOCK_METHOD(void, onPing, ());
  MOCK_METHOD(void, onAuthRequest, (const std::string& scheme));
  MOCK_METHOD(void, onGetDataRequest, (const std::string& path, bool watch));
  MOCK_METHOD(void, onCreateRequest, (const std::string& path, CreateFlags flags, OpCodes opcode));
  MOCK_METHOD(void, onSetRequest, (const std::string& path));
  MOCK_METHOD(void, onGetChildrenRequest, (const std::string& path, bool watch, bool v2));
  MOCK_METHOD(void, onGetEphemeralsRequest, (const std::string& path));
  MOCK_METHOD(void, onGetAllChildrenNumberRequest, (const std::string& path));
  MOCK_METHOD(void, onDeleteRequest, (const std::string& path, int32_t version));
  MOCK_METHOD(void, onExistsRequest, (const std::string& path, bool watch));
  MOCK_METHOD(void, onGetAclRequest, (const std::string& path));
  MOCK_METHOD(void, onSetAclRequest, (const std::string& path, int32_t version));
  MOCK_METHOD(void, onSyncRequest, (const std::string& path));
  MOCK_METHOD(void, onCheckRequest, (const std::string& path, int32_t version));
  MOCK_METHOD(void, onMultiRequest, ());
  MOCK_METHOD(void, onReconfigRequest, ());
  MOCK_METHOD(void, onSetWatchesRequest, ());
  MOCK_METHOD(void, onCheckWatchesRequest, (const std::string& path, int32_t type));
  MOCK_METHOD(void, onRemoveWatchesRequest, (const std::string& path, int32_t type));
  MOCK_METHOD(void, onCloseRequest, ());
  MOCK_METHOD(void, onResponseBytes, (uint64_t bytes));
  MOCK_METHOD(void, onConnectResponse,
              (int32_t proto_version, int32_t timeout, bool readonly,
               const std::chrono::milliseconds& latency));
  MOCK_METHOD(void, onResponse,
              (OpCodes opcode, int32_t xid, int64_t zxid, int32_t error,
               const std::chrono::milliseconds& latency));
  MOCK_METHOD(void, onWatchEvent,
              (int32_t event_type, int32_t client_state, const std::string& path, int64_t zxid,
               int32_t error));
};

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
