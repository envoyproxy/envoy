#pragma once
#include <cstdint>

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class MySQLSession : Logger::Loggable<Logger::Id::filter> {
public:
  enum class State {
    MYSQL_INIT = 0,
    MYSQL_CHALLENGE_REQ = 1,
    MYSQL_CHALLENGE_RESP_41 = 2,
    MYSQL_CHALLENGE_RESP_320 = 3,
    MYSQL_SSL_PT = 4,
    MYSQL_AUTH_SWITCH_REQ = 5,
    MYSQL_AUTH_SWITCH_REQ_OLD = 6,
    MYSQL_AUTH_SWITCH_RESP = 7,
    MYSQL_AUTH_SWITCH_MORE = 8,
    MYSQL_REQ_RESP = 9,
    MYSQL_REQ = 10,
    MYSQL_RESYNC = 11,
    MYSQL_NOT_HANDLED = 12,
    MYSQL_ERROR = 13,
  };

  void setState(MySQLSession::State state) { state_ = state; }
  MySQLSession::State getState() { return state_; }
  uint8_t getExpectedSeq() { return expected_seq_; }
  void setExpectedSeq(uint8_t seq) { expected_seq_ = seq; }

private:
  MySQLSession::State state_{State::MYSQL_INIT};
  uint8_t expected_seq_{0};
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
