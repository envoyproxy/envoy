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
  uint8_t getExpectedSeq() { return expected_seq_; }
  void setExpectedSeq(uint8_t seq) { expected_seq_ = seq; }

private:
  MySQLSession::State state_{State::Init};
  uint8_t expected_seq_{0};
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
