#pragma once
#include "common/buffer/buffer_impl.h"

namespace Envoy {
    namespace Extensions {
namespace NetworkFilters {
namespace MysqlProxy {

class MysqlCodecQuery : public MysqlCodec {
private:
#define USE "use"
#define DESCRIBE "describe"
#define INSERT "insert"
#define ALTER "alter"

  MysqlCodec::Cmd cmd_;
  std::string op_;
  std::string statement_;

public:
  int Decode(Buffer::Instance& buffer);
  std::string Encode();
  MysqlCodec::Cmd GetCmd() { return cmd_; }
  std::string& GetOp() { return op_; }
  std::string& GetStatment() { return statement_; }
  void SetCmd(MysqlCodec::Cmd cmd);
  void SetOp(std::string& op);
  void SetStatement(std::string& statement);
};

} // namespace MysqlProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
