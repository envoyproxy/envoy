#pragma once

#include <set>
#include <string>
#include <vector>

#include "source/common/common/macros.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

// Type alias for command-subcommand validation mapping
using CommandSubcommandMap = absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>>;

struct SupportedCommands {
  /**
   * @return commands which hash to a single server
   */
  static const absl::flat_hash_set<std::string>& simpleCommands() {
    CONSTRUCT_ON_FIRST_USE(
        absl::flat_hash_set<std::string>, "append", "bf.add", "bf.card", "bf.exists", "bf.info",
        "bf.insert", "bf.loadchunk", "bf.madd", "bf.mexists", "bf.reserve", "bf.scandump",
        "bitcount", "bitfield", "bitpos", "decr", "decrby", "dump", "expire", "expireat", "geoadd",
        "geodist", "geohash", "geopos", "georadius_ro", "georadiusbymember_ro", "geosearch", "get",
        "getbit", "getdel", "getex", "getrange", "getset", "hdel", "hexists", "hget", "hgetall",
        "hincrby", "hincrbyfloat", "hkeys", "hlen", "hmget", "hmset", "hscan", "hset", "hsetnx",
        "hstrlen", "hvals", "incr", "incrby", "incrbyfloat", "lindex", "linsert", "llen", "lmove",
        "lpop", "lpush", "lpushx", "lrange", "lrem", "lset", "ltrim", "persist", "pexpire",
        "pexpireat", "pfadd", "pfcount", "psetex", "pttl", "publish", "restore", "rpop", "rpush",
        "rpushx", "sadd", "scard", "set", "setbit", "setex", "setnx", "setrange", "sismember",
        "smembers", "spop", "srandmember", "srem", "sscan", "strlen", "ttl", "type", "xack", "xadd",
        "xautoclaim", "xclaim", "xdel", "xlen", "xpending", "xrange", "xrevrange", "xtrim", "zadd",
        "zcard", "zcount", "zincrby", "zlexcount", "zpopmin", "zpopmax", "zrange", "zrangebylex",
        "zrangebyscore", "zrank", "zrem", "zremrangebylex", "zremrangebyrank", "zremrangebyscore",
        "zrevrange", "zrevrangebylex", "zrevrangebyscore", "zrevrank", "zscan", "zscore", "copy",
        "rpoplpush", "smove", "sunion", "sdiff", "sinter", "sinterstore", "zunionstore",
        "zinterstore", "pfmerge", "georadius", "georadiusbymember", "rename", "sort", "sort_ro",
        "zmscore", "sdiffstore", "msetnx", "substr", "zrangestore", "zunion", "zdiff",
        "sunionstore", "smismember", "hrandfield", "geosearchstore", "zdiffstore", "zinter",
        "zrandmember", "bitop", "lpos", "renamenx");
  }

  /**
   * @return multi-key commands
   */
  static const absl::flat_hash_set<std::string>& multiKeyCommands() {
    CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>, "del", "mget", "mset", "touch",
                           "unlink", "msetnx");
  }

  /**
   * @return commands which hash on the fourth argument
   */
  static const absl::flat_hash_set<std::string>& evalCommands() {
    CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>, "eval", "evalsha");
  }

  /**
   * @return commands which are sent to multiple servers and coalesced by summing the responses
   */
  static const absl::flat_hash_set<std::string>& hashMultipleSumResultCommands() {
    CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>, "del", "exists", "touch", "unlink");
  }

  /**
   * @return commands without keys which are sent to all redis shards and the responses are handled
   * using special response handler according  to its response type
   */
  static const absl::flat_hash_set<std::string>& ClusterScopeCommands() {
    CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>, "script", "flushall", "flushdb",
                           "slowlog", "config", "info", "keys", "select", "role");
  }

  /**
   * @return commands without keys which are sent to a single random shard
   */
  static const absl::flat_hash_set<std::string>& randomShardCommands() {
    CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>, "cluster", "randomkey");
  }

  /**
   * @return map of commands to their supported subcommands
   * If a command is not in this map, all its subcommands are supported
   * If a command is in this map, only the listed subcommands are supported
   */
  static const CommandSubcommandMap& commandSubcommandValidationMap() {
    CONSTRUCT_ON_FIRST_USE(CommandSubcommandMap,
                           // Command name - Sub commands that are allowed
                           {{"cluster", {"info", "slots", "keyslot", "nodes"}}});
    // Add other commands with restricted subcommands here:
  }

  /**
   * @return commands which handle Redis transactions.
   */
  static const absl::flat_hash_set<std::string>& transactionCommands() {
    CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>, "multi", "exec", "discard", "watch",
                           "unwatch");
  }

  /**
   * @return auth command
   */
  static const std::string& auth() { CONSTRUCT_ON_FIRST_USE(std::string, "auth"); }

  /**
   * @return echo command
   */
  static const std::string& echo() { CONSTRUCT_ON_FIRST_USE(std::string, "echo"); }

  /**
   * @return mget command
   */
  static const std::string& mget() { CONSTRUCT_ON_FIRST_USE(std::string, "mget"); }

  /**
   * @return mset command
   */
  static const std::string& mset() { CONSTRUCT_ON_FIRST_USE(std::string, "mset"); }

  /**
   * @return ping command
   */
  static const std::string& ping() { CONSTRUCT_ON_FIRST_USE(std::string, "ping"); }

  /**
   * @return time command
   */
  static const std::string& time() { CONSTRUCT_ON_FIRST_USE(std::string, "time"); }

  /**
   * @return quit command
   */
  static const std::string& quit() { CONSTRUCT_ON_FIRST_USE(std::string, "quit"); }

  /**
   * @return scan command
   */
  static const std::string& scan() { CONSTRUCT_ON_FIRST_USE(std::string, "scan"); }

  /**
   * @return info.shard command
   */
  static const std::string& infoShard() { CONSTRUCT_ON_FIRST_USE(std::string, "info.shard"); }

  /**
   * @return commands which alters the state of redis
   */
  static const absl::flat_hash_set<std::string>& writeCommands() {
    CONSTRUCT_ON_FIRST_USE(
        absl::flat_hash_set<std::string>, "append", "bitfield", "decr", "decrby", "del", "discard",
        "exec", "expire", "expireat", "eval", "evalsha", "geoadd", "getdel", "hdel", "hincrby",
        "hincrbyfloat", "hmset", "hset", "hsetnx", "incr", "incrby", "incrbyfloat", "linsert",
        "lmove", "lpop", "lpush", "lpushx", "lrem", "lset", "ltrim", "mset", "multi", "persist",
        "pexpire", "pexpireat", "pfadd", "psetex", "restore", "rpop", "rpush", "rpushx", "sadd",
        "set", "setbit", "setex", "setnx", "setrange", "spop", "srem", "zadd", "zincrby", "touch",
        "zpopmin", "zpopmax", "zrem", "zremrangebylex", "zremrangebyrank", "zremrangebyscore",
        "unlink", "copy", "rpoplpush", "smove", "sinterstore", "zunionstore", "zinterstore",
        "pfmerge", "georadius", "georadiusbymember", "rename", "sort", "sdiffstore", "msetnx",
        "zrangestore", "sunionstore", "geosearchstore", "zdiffstore", "bitop", "renamenx");
  }

  static bool isReadCommand(const std::string& command) {
    return !writeCommands().contains(command);
  }

  /**
   * @return commands that are valid without mandatory arguments beyond the command name
   */
  static const absl::flat_hash_set<std::string>& commandsWithoutMandatoryArgs() {
    CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>,
                           "ping",      // PING [message]
                           "time",      // TIME
                           "flushall",  // FLUSHALL [ASYNC]
                           "flushdb",   // FLUSHDB [ASYNC]
                           "randomkey", // RANDOMKEY
                           "quit",      // QUIT
                           "role",      // ROLE
                           "info"       // INFO [section]
    );
  }

  /**
   * @return true if the command can be executed without mandatory arguments beyond command name
   */
  static bool isCommandValidWithoutArgs(const std::string& command_name);

  /**
   * @brief Validates if a subcommand is allowed for the given command
   * @param command the main command name (e.g., "cluster") - should be lowercase
   * @param subcommand the subcommand to validate (e.g., "info") - should be lowercase
   * @return true if subcommand is valid or no validation needed, false if invalid subcommand
   */
  static bool validateCommandSubcommands(const std::string& command, const std::string& subcommand);

  static bool isSupportedCommand(const std::string& command);
};

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
