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
    MYSQL_NOT_HANDLED = 11,
    MYSQL_ERROR = 12,
  };

  MySQLSession() { cinfo_ = {}; };
  uint64_t getId() { return cinfo_.id_; }
  void setId(uint64_t id) { cinfo_.id_ = id; }
  void setState(MySQLSession::State state) { cinfo_.state_ = state; }
  MySQLSession::State getState() { return cinfo_.state_; }
  int getExpectedSeq() { return cinfo_.expected_seq_; }
  void setExpectedSeq(int seq) { cinfo_.expected_seq_ = seq; }

private:
  struct ConnInfo {
    uint64_t id_;
    MySQLSession::State state_;
    int expected_seq_;
  };

  ConnInfo cinfo_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
