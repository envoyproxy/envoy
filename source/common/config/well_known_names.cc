#include "source/common/config/well_known_names.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Config {

namespace {

const absl::string_view TAG_VALUE_REGEX = R"([^\.]+)";

// To allow for more readable regular expressions to be declared below, and to
// reduce duplication, define a few common pattern substitutions for regex
// segments.
std::string expandRegex(const std::string& regex) {
  return absl::StrReplaceAll(
      regex, {// Regex to look for either IPv4 or IPv6 addresses plus port number after underscore.
              {"<ADDRESS>", R"((?:(?:\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}|\[[a-fA-F_\d]+\])_\d+))"},
              // Cipher names can contain alphanumerics with dashes and
              // underscores.
              {"<CIPHER>", R"([\w-]+)"},
              // TLS version strings are always of the form "TLSv1.3".
              {"<TLS_VERSION>", R"(TLSv\d\.\d)"},
              // A generic name can contain any character except dots.
              {"<TAG_VALUE>", TAG_VALUE_REGEX},
              // Route names may contain dots in addition to alphanumerics and
              // dashes with underscores.
              {"<ROUTE_CONFIG_NAME>", R"([\w-\.]+)"},
              // Match a prefix that is either a listener plus name or cluster plus name
              {"<LISTENER_OR_CLUSTER_WITH_NAME>", R"((?:listener|cluster)\..*?)"}});
}

const Regex::CompiledGoogleReMatcher& validTagValueRegex() {
  CONSTRUCT_ON_FIRST_USE(Regex::CompiledGoogleReMatcher, absl::StrCat("^", TAG_VALUE_REGEX, "$"),
                         false);
}

} // namespace

bool doesTagNameValueMatchInvalidCharRegex(absl::string_view name) {
  return validTagValueRegex().match(name);
}

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
      R"(^http\.<TAG_VALUE>\.dynamodb\.table\.<TAG_VALUE>\.capacity\.<TAG_VALUE>(\.__partition_id=(\w{7}))$)",
      ".dynamodb.table.");

  // http.[<stat_prefix>.]dynamodb.operation.(<operation_name>.)* or
  // http.[<stat_prefix>.]dynamodb.table.[<table_name>.]capacity.(<operation_name>.)[<partition_id>]
  addRe2(
      DYNAMO_OPERATION,
      R"(^http\.<TAG_VALUE>\.dynamodb.(?:operation|table\.<TAG_VALUE>\.capacity)(\.(<TAG_VALUE>))(?:\.|$))",
      ".dynamodb.");

  // mongo.[<stat_prefix>.]collection.[<collection>.]callsite.(<callsite>.)query.*
  addTokenized(MONGO_CALLSITE, "mongo.*.collection.*.callsite.$.query.**");

  // http.[<stat_prefix>.]dynamodb.table.(<table_name>.)* or
  // http.[<stat_prefix>.]dynamodb.error.(<table_name>.)*
  addRe2(DYNAMO_TABLE, R"(^http\.<TAG_VALUE>\.dynamodb.(?:table|error)\.((<TAG_VALUE>)\.))",
         ".dynamodb.");

  // mongo.[<stat_prefix>.]collection.(<collection>.)query.*
  addTokenized(MONGO_COLLECTION, "mongo.*.collection.$.**.query.*");

  // mongo.[<stat_prefix>.]cmd.(<cmd>.)*
  addTokenized(MONGO_CMD, "mongo.*.cmd.$.**");

  // cluster.[<route_target_cluster>.]grpc.[<grpc_service>.](<grpc_method>.)*
  addTokenized(GRPC_BRIDGE_METHOD, "cluster.*.grpc.*.$.**");

  // http.[<stat_prefix>.]user_agent.(<user_agent>.)*
  addTokenized(HTTP_USER_AGENT, "http.*.user_agent.$.**");

  // vhost.[<virtual host name>.]vcluster.(<virtual_cluster_name>.)*
  addTokenized(VIRTUAL_CLUSTER, "vhost.*.vcluster.$.**");

  // http.[<stat_prefix>.]fault.(<downstream_cluster>.)*
  addTokenized(FAULT_DOWNSTREAM_CLUSTER, "http.*.fault.$.**");

  // listener.[<address>.]ssl.ciphers.(<cipher>)
  addRe2(SSL_CIPHER, R"(^listener\..*?\.ssl\.ciphers(\.(<CIPHER>))$)", ".ssl.ciphers.");

  // Curves and ciphers have the same pattern so they use the same regex.
  // listener.[<address>.]ssl.curves.(<curve>)
  addRe2(SSL_CURVE, R"(^<LISTENER_OR_CLUSTER_WITH_NAME>\.ssl\.curves(\.(<CIPHER>))$)",
         ".ssl.curves.");

  // Signing algorithms and ciphers have the same pattern so they use the same regex.
  // listener.[<address>.]ssl.sigalgs.(<algorithm>)
  addRe2(SSL_SIGALG, R"(^<LISTENER_OR_CLUSTER_WITH_NAME>\.ssl\.sigalgs(\.(<CIPHER>))$)",
         ".ssl.sigalgs.");

  // listener.[<address>.]ssl.versions.(<version>)
  addRe2(SSL_VERSION, R"(^<LISTENER_OR_CLUSTER_WITH_NAME>\.ssl\.versions(\.(<TLS_VERSION>))$)",
         ".ssl.versions.");

  // cluster.[<cluster_name>.]ssl.ciphers.(<cipher>)
  addRe2(SSL_CIPHER_SUITE, R"(^cluster\.<TAG_VALUE>\.ssl\.ciphers(\.(<CIPHER>))$)",
         ".ssl.ciphers.");

  // cluster.[<route_target_cluster>.]grpc.(<grpc_service>.)*
  addTokenized(GRPC_BRIDGE_SERVICE, "cluster.*.grpc.$.**");

  // tcp.(<stat_prefix>.)*
  addTokenized(TCP_PREFIX, "tcp.$.**");

  // udp.(<stat_prefix>.)*
  addTokenized(UDP_PREFIX, "udp.$.**");

  // auth.clientssl.(<stat_prefix>.)*
  addTokenized(CLIENTSSL_PREFIX, "auth.clientssl.$.**");

  // ratelimit.(<stat_prefix>.)*
  addTokenized(RATELIMIT_PREFIX, "ratelimit.$.**");

  // cluster.(<cluster_name>.)*
  addTokenized(CLUSTER_NAME, "cluster.$.**");

  // listener.[<address>.]http.(<stat_prefix>.)*
  // The <address> part can be anything here (.*?) for the sake of a simpler
  // internal state of the regex which performs better.
  addRe2(HTTP_CONN_MANAGER_PREFIX, R"(^listener\..*?\.http\.((<TAG_VALUE>)\.))", ".http.");

  // Extract ext_authz stat_prefix field
  // cluster.[<cluster>.]ext_authz.[<ext_authz_prefix>.]*
  addTokenized(EXT_AUTHZ_PREFIX, "cluster.*.ext_authz.$.**");
  // http.[<http_conn_mgr_prefix>.]ext_authz.[<ext_authz_prefix>.]*
  addTokenized(EXT_AUTHZ_PREFIX, "http.*.ext_authz.$.**");

  // http.(<stat_prefix>.)*
  addTokenized(HTTP_CONN_MANAGER_PREFIX, "http.$.**");

  // listener.<address|stat_prefix>.(worker_<id>.)*
  // listener_manager.(worker_<id>.)*
  // server.(worker_<id>.)*
  addRe2(
      WORKER_ID,
      R"(^(?:listener\.(?:<ADDRESS>|<TAG_VALUE>)\.|server\.|listener_manager\.)worker_((\d+)\.))",
      "");

  // listener.(<address|stat_prefix>.)*, but specifically excluding "admin"
  addRe2(LISTENER_ADDRESS, R"(^listener\.((<ADDRESS>|<TAG_VALUE>)\.))", "", "admin");

  // vhost.(<virtual host name>.)*
  addTokenized(VIRTUAL_HOST, "vhost.$.**");

  // mongo.(<stat_prefix>.)*
  addTokenized(MONGO_PREFIX, "mongo.$.**");

  // http.[<stat_prefix>.]rds.(<route_config_name>.)<base_stat>
  // Note: <route_config_name> can contain dots thus we have to maintain full
  // match.
  addRe2(RDS_ROUTE_CONFIG, R"(^http\.<TAG_VALUE>\.rds\.((<ROUTE_CONFIG_NAME>)\.)\w+?$)", ".rds.");

  // vhost.[<virtual host name>.]route.(<route_stat_prefix>.)*
  addTokenized(ROUTE, "vhost.*.route.$.**");

  // thrift.(<stat_prefix>.)*
  addTokenized(THRIFT_PREFIX, "thrift.$.**");

  // redis.(<stat_prefix>.)*
  addTokenized(REDIS_PREFIX, "redis.$.**");

  // (<stat_prefix>.).http_local_rate_limit.**
  addTokenized(LOCAL_HTTP_RATELIMIT_PREFIX, "$.http_local_rate_limit.**");

  // local_rate_limit.(<stat_prefix>.)
  addTokenized(LOCAL_NETWORK_RATELIMIT_PREFIX, "local_rate_limit.$.**");

  // listener_local_rate_limit.(<stat_prefix>.)
  addTokenized(LOCAL_LISTENER_RATELIMIT_PREFIX, "listener_local_ratelimit.$.**");

  // dns_filter.(<stat_prefix>.).**
  addTokenized(DNS_FILTER_PREFIX, "dns_filter.$.**");
}

void TagNameValues::addRe2(const std::string& name, const std::string& regex,
                           const std::string& substr, const std::string& negative_matching_value) {
  descriptor_vec_.emplace_back(
      Descriptor{name, expandRegex(regex), substr, negative_matching_value, Regex::Type::Re2});
}

void TagNameValues::addTokenized(const std::string& name, const std::string& tokens) {
  tokenized_descriptor_vec_.emplace_back(TokenizedDescriptor{name, tokens});
}

} // namespace Config
} // namespace Envoy
