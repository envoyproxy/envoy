#pragma once

#include <set>
#include <string>
#include <vector>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

#include "common/common/macros.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

class SupportedCommands {

public:
  SupportedCommands() {
    simple_ = absl::flat_hash_set<std::string>{"append",
                                               "bitcount",
                                               "bitfield",
                                               "bitpos",
                                               "decr",
                                               "decrby",
                                               "dump",
                                               "expire",
                                               "expireat",
                                               "geoadd",
                                               "geodist",
                                               "geohash",
                                               "geopos",
                                               "georadius_ro",
                                               "georadiusbymember_ro",
                                               "get",
                                               "getbit",
                                               "getrange",
                                               "getset",
                                               "hdel",
                                               "hexists",
                                               "hget",
                                               "hgetall",
                                               "hincrby",
                                               "hincrbyfloat",
                                               "hkeys",
                                               "hlen",
                                               "hmget",
                                               "hmset",
                                               "hscan",
                                               "hset",
                                               "hsetnx",
                                               "hstrlen",
                                               "hvals",
                                               "incr",
                                               "incrby",
                                               "incrbyfloat",
                                               "lindex",
                                               "linsert",
                                               "llen",
                                               "lpop",
                                               "lpush",
                                               "lpushx",
                                               "lrange",
                                               "lrem",
                                               "lset",
                                               "ltrim",
                                               "persist",
                                               "pexpire",
                                               "pexpireat",
                                               "pfadd",
                                               "pfcount",
                                               "psetex",
                                               "pttl",
                                               "restore",
                                               "rpop",
                                               "rpush",
                                               "rpushx",
                                               "sadd",
                                               "scard",
                                               "set",
                                               "setbit",
                                               "setex",
                                               "setnx",
                                               "setrange",
                                               "sismember",
                                               "smembers",
                                               "spop",
                                               "srandmember",
                                               "srem",
                                               "sscan",
                                               "strlen",
                                               "ttl",
                                               "type",
                                               "zadd",
                                               "zcard",
                                               "zcount",
                                               "zincrby",
                                               "zlexcount",
                                               "zpopmin",
                                               "zpopmax",
                                               "zrange",
                                               "zrangebylex",
                                               "zrangebyscore",
                                               "zrank",
                                               "zrem",
                                               "zremrangebylex",
                                               "zremrangebyrank",
                                               "zremrangebyscore",
                                               "zrevrange",
                                               "zrevrangebylex",
                                               "zrevrangebyscore",
                                               "zrevrank",
                                               "zscan",
                                               "zscore"};
    eval_ = absl::flat_hash_set<std::string>{"eval", "evalsha"};
    multi_sum_ = absl::flat_hash_set<std::string>{"del", "exists", "touch", "unlink"};
    mget_ = absl::flat_hash_set<std::string>{"mget"};
    mset_ = absl::flat_hash_set<std::string>{"mset"};
    write_ = absl::flat_hash_set<std::string>{"append",
                                              "bitfield",
                                              "decr",
                                              "decrby",
                                              "del",
                                              "expire",
                                              "expireat",
                                              "eval",
                                              "evalsha",
                                              "geoadd",
                                              "hdel",
                                              "hincrby",
                                              "hincrbyfloat",
                                              "hmset",
                                              "hset",
                                              "hsetnx",
                                              "incr",
                                              "incrby",
                                              "incrbyfloat",
                                              "linsert",
                                              "lpop",
                                              "lpush",
                                              "lpushx",
                                              "lrem",
                                              "lset",
                                              "ltrim",
                                              "mset",
                                              "persist",
                                              "pexpire",
                                              "pexpireat",
                                              "pfadd",
                                              "psetex",
                                              "restore",
                                              "rpop",
                                              "rpush",
                                              "rpushx",
                                              "sadd",
                                              "set",
                                              "setbit",
                                              "setex",
                                              "setnx",
                                              "setrange",
                                              "spop",
                                              "srem",
                                              "zadd",
                                              "zincrby",
                                              "touch",
                                              "zpopmin",
                                              "zpopmax",
                                              "zrem",
                                              "zremrangebylex",
                                              "zremrangebyrank",
                                              "zremrangebyscore",
                                              "unlink"};
  }

  /**
   * @return commands which hash to a single server
   */
  const absl::flat_hash_set<std::string>& simpleCommands() { return simple_; }

  /**
   * @return commands which hash on the fourth argument
   */
  const absl::flat_hash_set<std::string>& evalCommands() { return eval_; }

  /**
   * @return commands which are sent to multiple servers and coalesced by summing the responses
   */
  const absl::flat_hash_set<std::string>& hashMultipleSumResultCommands() { return multi_sum_; }

  /**
   * @return mget command
   */
  const absl::flat_hash_set<std::string>& mget() { return mget_; }

  /**
   * @return mset command
   */
  const absl::flat_hash_set<std::string>& mset() { return mset_; }

  /**
   * @return commands which alters the state of redis
   */
  const absl::flat_hash_set<std::string>& writeCommands() { return write_; }

  /**
   * @return auth command
   */
  static const std::string& auth() { CONSTRUCT_ON_FIRST_USE(std::string, "auth"); }

  /**
   * @return ping command
   */
  static const std::string& ping() { CONSTRUCT_ON_FIRST_USE(std::string, "ping"); }

  bool isReadCommand(const std::string& command) { return !writeCommands().contains(command); }

  void
  addModules(const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy& proto_config) {
    for (auto const& module : proto_config.modules()) {
      for (const std::string& command : module.simple()) {
        simple_.insert(absl::AsciiStrToLower(command));
      }
      for (const std::string& command : module.mget()) {
        mget_.insert(absl::AsciiStrToLower(command));
      }
      for (const std::string& command : module.mset()) {
        mset_.insert(absl::AsciiStrToLower(command));
      }
      for (const std::string& command : module.multi_sum()) {
        multi_sum_.insert(absl::AsciiStrToLower(command));
      }
      for (const std::string& command : module.write()) {
        write_.insert(absl::AsciiStrToLower(command));
      }
    }
  }

private:
  absl::flat_hash_set<std::string> simple_;
  absl::flat_hash_set<std::string> eval_;
  absl::flat_hash_set<std::string> multi_sum_;
  absl::flat_hash_set<std::string> mget_;
  absl::flat_hash_set<std::string> mset_;
  absl::flat_hash_set<std::string> write_;
};

using SupportedCommandsSharedPtr = std::shared_ptr<SupportedCommands>;

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
