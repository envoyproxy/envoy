#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

const int CONNECT_XID = 0;
const int WATCH_XID = -1;
const int PING_XID = -2;
const int AUTH_XID = -4;
const int SET_WATCHES_XID = -8;

enum class Opcodes {
  CONNECT = 0,
  CREATE = 1,
  DELETE = 2,
  EXISTS = 3,
  GETDATA = 4,
  SETDATA = 5,
  GETACL = 6,
  SETACL = 7,
  GETCHILDREN = 8,
  SYNC = 9,
  PING = 11,
  GETCHILDREN2 = 12,
  CHECK = 13,
  MULTI = 14,
  CREATE2 = 15,
  RECONFIG = 16,
  CREATESESSION = -10,
  CLOSE = -11,
  SETAUTH = 100,
  SETWATCHES = 101
};

/**
 * General callbacks for dispatching decoded ZooKeeper messages to a sink.
 */
class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() {}

  virtual void onDecodeError() PURE;
  virtual void onConnect(const bool readonly) PURE;
  virtual void onPing() PURE;
  virtual void onAuthRequest(const std::string& scheme) PURE;
  virtual void onGetDataRequest(const std::string& path, const bool watch) PURE;
  virtual void onCreateRequest(const std::string& path, const bool ephemeral, const bool sequence,
                               const bool two) PURE;
  virtual void onSetRequest(const std::string& path) PURE;
  virtual void onGetChildrenRequest(const std::string& path, const bool watch, const bool two) PURE;
};

/**
 * ZooKeeper message decoder.
 */
class Decoder {
public:
  virtual ~Decoder() {}

  virtual void onData(Buffer::Instance& data) PURE;
};

typedef std::unique_ptr<Decoder> DecoderPtr;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
public:
  explicit DecoderImpl(DecoderCallbacks& callbacks) : callbacks_(callbacks) {}

  // ZooKeeperProxy::Decoder
  void onData(Buffer::Instance& data) override;

private:
  void decode(Buffer::Instance& data, uint64_t& offset);
  void parseConnect(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseAuthRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseGetDataRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseCreateRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len, const bool two);
  void skipAcls(Buffer::Instance& data, uint64_t& offset) const;
  void parseSetRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseGetChildrenRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                               const bool two);

  DecoderCallbacks& callbacks_;
};

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
