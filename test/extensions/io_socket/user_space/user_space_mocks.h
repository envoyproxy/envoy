#pragma once

#include "source/extensions/io_socket/user_space/io_handle.h"

#include "test/mocks/network/io_handle.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace UserSpace {

class MockUserSpaceIoHandle : public Network::MockIoHandle, public IoHandle {
public:
  MOCK_METHOD(void, setEof, ());
  MOCK_METHOD(void, setRst, ());
  MOCK_METHOD(bool, hasReceivedEof, (), (const));
  MOCK_METHOD(void, onPeerDestroy, ());
  MOCK_METHOD(void, setNewDataAvailable, ());
  MOCK_METHOD(Buffer::Instance*, getReceiveBuffer, ());
  MOCK_METHOD(bool, canReceiveData, (), (const));
  MOCK_METHOD(bool, isWriteUnblocked, (), (const));
  MOCK_METHOD(void, onPeerBufferLowWatermark, ());
  MOCK_METHOD(bool, isReadable, (), (const));
  MOCK_METHOD(std::shared_ptr<PassthroughState>, passthroughState, ());
};

class MockPassthroughState : public PassthroughState {
public:
  MOCK_METHOD(void, initialize,
              (std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
               const StreamInfo::FilterState::Objects& filter_state_objects));
  MOCK_METHOD(void, mergeInto,
              (envoy::config::core::v3::Metadata & metadata,
               StreamInfo::FilterState& filter_state));
};

} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
