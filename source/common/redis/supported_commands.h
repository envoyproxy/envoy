#pragma once

#include <string>
#include <vector>

namespace Envoy {
namespace Redis {

struct SupportedCommands {
  /**
   * @return commands which hash to a single server
   */
  static const std::vector<std::string>& simpleCommands() {
    static const std::vector<std::string>* commands =
        new std::vector<std::string>{"append",
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
    return *commands;
  }

  /**
   * @return commands which are sent to multiple servers and coalesced by summing the responses
   */
  static const std::vector<std::string>& hashMultipleSumResultCommands() {
    static const std::vector<std::string>* commands =
        new std::vector<std::string>{"del", "exists", "touch", "unlink"};
    return *commands;
  }

  /**
   * @return mget command
   */
  static const std::string& mget() {
    static const std::string* command = new std::string("mget");
    return *command;
  }

  /**
   * @return mset command
   */
  static const std::string& mset() {
    static const std::string* command = new std::string("mset");
    return *command;
  }

  // TODO(danielhochman): support for EVAL with argument enforcement
};

} // namespace Redis
} // namespace Envoy
