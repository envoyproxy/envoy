#pragma once
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class MySQLSession : Logger::Loggable<Logger::Id::filter> {
public:
  enum class State {
    Init = 0,
    ChallengeReq = 1,
    ChallengeResp41 = 2,
    ChallengeResp320 = 3,
    SslPt = 4,
    AuthSwitchReq = 5,
    AuthSwitchReqOld = 6,
    AuthSwitchResp = 7,
    AuthSwitchMore = 8,
    ReqResp = 9,
    Req = 10,
    Resync = 11,
    NotHandled = 12,
    Error = 13,
  };

  void setState(MySQLSession::State state) { state_ = state; }
  MySQLSession::State getState() { return state_; }
  uint8_t getExpectedSeq(bool is_upstream) { return seq_ - (is_upstream ? 0 : seq_offset_); }
  uint8_t convertToSeqOnReciever(uint8_t seq, bool is_upstream) {
    return seq - (is_upstream ? 1 : -1) * seq_offset_;
  }
  void resetSeq() {
    seq_ = MYSQL_REQUEST_PKT_NUM;
    seq_offset_ = 0;
  }
  void incSeq() { seq_++; }
  int8_t getSeqOffset() const { return seq_offset_; }
  void setSeqOffset(int8_t offset) { seq_offset_ = offset; }
  void adjustSeqOffset(int8_t delta) { seq_offset_ += delta; }

private:
  MySQLSession::State state_{State::Init};
  uint8_t seq_{0};
  int8_t seq_offset_{0};
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
