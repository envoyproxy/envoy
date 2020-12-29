#include "common/config/well_known_names.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Config {

namespace {

const std::string& wordRegex() { CONSTRUCT_ON_FIRST_USE(std::string, R"([^\.]+)"); }

std::string expandRegex(const std::string& regex) {
  return absl::StrReplaceAll(
      regex, {{"<ADDRESS>",
               R"((?:\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}_\d+|\[[_aAbBcCdDeEfF[:digit:]]+\]_\d+))"},
              {"<CALLSITE>", wordRegex()},
              {"<CIPHER>", R"([0-9A-Za-z_-]+)"},
              {"<CLUSTER_NAME>", wordRegex()},
              {"<CMD>", wordRegex()},
              {"<COLLECTION>", wordRegex()},
              {"<DOWNSTREAM_CLUSTER>", wordRegex()},
              {"<GRPC_METHOD>", wordRegex()},
              {"<GRPC_SERVICE>", wordRegex()},
              {"<OPERATION_NAME>", wordRegex()},
              {"<ROUTE_CONFIG_NAME>", R"([\w-\.]+)"},
              {"<STAT_PREFIX>", wordRegex()},
              {"<TABLE_NAME>", wordRegex()},
              {"<USER_AGENT>", wordRegex()},
              {"<VIRTUAL_CLUSTER_NAME>", wordRegex()},
              {"<VIRTUAL_HOST_NAME>", wordRegex()}});
}

} // namespace

TagNameValues::TagNameValues() {
  // Note: the default regexes are defined below in the order that they will typically be matched
  // (see the TagExtractor class definition for an explanation of the iterative matching process).
  // This ordering is roughly from most specific to least specific. Despite the fact that these
  // regexes are defined with a particular ordering in mind, users can customize the ordering of the
  // processing of the default tag extraction regexes and include custom tags with regexes via the
  // bootstrap configuration. Because of this flexibility, these regexes are designed to not
  // interfere with one another no matter the ordering. They are tested in forward and reverse
  // ordering to ensure they will be safe in most ordering configurations.

  // To give a more user-friendly explanation of the intended behavior of each regex, each is
  // preceded by a comment with a simplified notation to explain what the regex is designed to
  // match:
  // - The text that the regex is intended to capture will be enclosed in ().
  // - Other default tags that are expected to exist in the name (and may or may not have been
  // removed before this regex has been applied) are enclosed in [].
  // - Stand-ins for a variable segment of the name (including inside capture groups) will be
  // enclosed in <>.
  // - Typical * notation will be used to denote an arbitrary set of characters.

  // *_rq(_<response_code>)
  addRe2(RESPONSE_CODE, R"(_rq(_(\d{3}))$)", "_rq_");

  // *_rq_(<response_code_class>)xx
  addRe2(RESPONSE_CODE_CLASS, R"(_rq_((\d))xx$)", "_rq_");

  // http.[<stat_prefix>.]dynamodb.table.[<table_name>.]capacity.[<operation_name>.](__partition_id=<last_seven_characters_from_partition_id>)
  addRe2(
      DYNAMO_PARTITION_ID,
      R"(^http\.<STAT_PREFIX>\.dynamodb\.table\.<TABLE_NAME>\.capacity\.<OPERATION_NAME>(\.__partition_id=(\w{7}))$)",
      ".dynamodb.table.");

  // http.[<stat_prefix>.]dynamodb.operation.(<operation_name>.)<base_stat> or
  // http.[<stat_prefix>.]dynamodb.table.[<table_name>.]capacity.(<operation_name>.)[<partition_id>]
  addRe2(
      DYNAMO_OPERATION,
      R"(^http\.<STAT_PREFIX>\.dynamodb.(?:operation|table\.<TABLE_NAME>\.capacity)(\.(<OPERATION_NAME>))(?:\.|$))",
      ".dynamodb.");

  // mongo.[<stat_prefix>.]collection.[<collection>.]callsite.(<callsite>.)query.<base_stat>
  addRe2(MONGO_CALLSITE,
         R"(^mongo\.<STAT_PREFIX>\.collection\.<COLLECTION>\.callsite\.((<CALLSITE>)\.)query\.)",
         ".collection.");

  // http.[<stat_prefix>.]dynamodb.table.(<table_name>.) or
  // http.[<stat_prefix>.]dynamodb.error.(<table_name>.)*
  addRe2(DYNAMO_TABLE, R"(^http\.<STAT_PREFIX>\.dynamodb.(?:table|error)\.((<TABLE_NAME>)\.))",
         ".dynamodb.");

  // mongo.[<stat_prefix>.]collection.(<collection>.)query.<base_stat>
  addRe2(MONGO_COLLECTION, R"(^mongo\.<STAT_PREFIX>\.collection\.((<COLLECTION>)\.).*?query\.)",
         ".collection.");

  // mongo.[<stat_prefix>.]cmd.(<cmd>.)<base_stat>
  addRe2(MONGO_CMD, R"(^mongo\.<STAT_PREFIX>\.cmd\.((<CMD>)\.))", ".cmd.");

  // cluster.[<route_target_cluster>.]grpc.<grpc_service>.(<grpc_method>.)*
  addRe2(GRPC_BRIDGE_METHOD,
         R"(^cluster\.<CLUSTER_NAME>\.grpc\.<GRPC_SERVICE>\.((<GRPC_METHOD>)\.))", ".grpc.");

  // http.[<stat_prefix>.]user_agent.(<user_agent>.)*
  addRe2(HTTP_USER_AGENT, R"(^http\.<STAT_PREFIX>\.user_agent\.((<USER_AGENT>)\.))",
         ".user_agent.");

  // vhost.[<virtual host name>.]vcluster.(<virtual_cluster_name>.)*
  addRe2(VIRTUAL_CLUSTER, R"(^vhost\.<VIRTUAL_HOST_NAME>\.vcluster\.((<VIRTUAL_CLUSTER_NAME>)\.))",
         ".vcluster.");

  // http.[<stat_prefix>.]fault.(<downstream_cluster>.)*
  addRe2(FAULT_DOWNSTREAM_CLUSTER, R"(^http\.<STAT_PREFIX>\.fault\.((<DOWNSTREAM_CLUSTER>)\.))",
         ".fault.");

  // listener.[<address>.]ssl.cipher.(<cipher>)
  addRe2(SSL_CIPHER, R"(^listener\..*?\.ssl\.cipher(\.(<CIPHER>))$)");

  // cluster.[<cluster_name>.]ssl.ciphers.(<cipher>)
  addRe2(SSL_CIPHER_SUITE, R"(^cluster\.<CLUSTER_NAME>\.ssl\.ciphers(\.(<CIPHER>))$)",
         ".ssl.ciphers.");

  // cluster.[<route_target_cluster>.]grpc.(<grpc_service>.)*
  addRe2(GRPC_BRIDGE_SERVICE, R"(^cluster\.<CLUSTER_NAME>\.grpc\.((<GRPC_SERVICE>)\.))", ".grpc.");

  // tcp.(<stat_prefix>.)<base_stat>
  addRe2(TCP_PREFIX, R"(^tcp\.((<STAT_PREFIX>)\.))");

  // udp.(<stat_prefix>.)<base_stat>
  addRe2(UDP_PREFIX, R"(^udp\.((<STAT_PREFIX>)\.))");

  // auth.clientssl.(<stat_prefix>.)*
  addRe2(CLIENTSSL_PREFIX, R"(^auth\.clientssl\.((<STAT_PREFIX>)\.))");

  // ratelimit.(<stat_prefix>.)*
  addRe2(RATELIMIT_PREFIX, R"(^ratelimit\.((<STAT_PREFIX>)\.))");

  // cluster.(<cluster_name>.)*
  addRe2(CLUSTER_NAME, R"(^cluster\.((<CLUSTER_NAME>)\.))");

  // listener.[<address>.]http.(<stat_prefix>.)*
  addRe2(HTTP_CONN_MANAGER_PREFIX, R"(^listener\..*?\.http\.((<STAT_PREFIX>)\.))", ".http.");

  // http.(<stat_prefix>.)*
  addRe2(HTTP_CONN_MANAGER_PREFIX, R"(^http\.((<STAT_PREFIX>)\.))");

  // listener.(<address>.)*
  addRe2(LISTENER_ADDRESS, R"(^listener\.((<ADDRESS>)\.))");

  // vhost.(<virtual host name>.)*
  addRe2(VIRTUAL_HOST, R"(^vhost\.((<VIRTUAL_HOST_NAME>)\.))");

  // mongo.(<stat_prefix>.)*
  addRe2(MONGO_PREFIX, R"(^mongo\.((<STAT_PREFIX>)\.))");

  // http.[<stat_prefix>.]rds.(<route_config_name>.)<base_stat>
  addRe2(RDS_ROUTE_CONFIG, R"(^http\.<STAT_PREFIX>\.rds\.((<ROUTE_CONFIG_NAME>)\.)\w+?$)", ".rds.");

  // listener_manager.(worker_<id>.)*
  addRe2(WORKER_ID, R"(^listener_manager\.((worker_\d+)\.))", "listener_manager.worker_");
}

void TagNameValues::addRegex(const std::string& name, const std::string& regex,
                             const std::string& substr) {
  descriptor_vec_.emplace_back(Descriptor{name, expandRegex(regex), substr, Regex::Type::StdRegex});
}

void TagNameValues::addRe2(const std::string& name, const std::string& regex,
                           const std::string& substr) {
  descriptor_vec_.emplace_back(Descriptor{name, expandRegex(regex), substr, Regex::Type::Re2});
}

} // namespace Config
} // namespace Envoy
