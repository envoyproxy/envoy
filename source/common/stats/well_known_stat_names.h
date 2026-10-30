#pragma once

#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Stats {

// Structural tokens that appear in the dot-delimited names of per-resource stats (per cluster,
// listener, HTTP connection manager, virtual host, ...). They are encoded when the symbol table
// is created, which pins them to the first symbol IDs so that each one costs a single byte in
// every stat name that references it. Only 127 symbols get the single-byte encoding (enforced by
// unit test), so the list is limited to the roots of the core per-resource stat families and
// deliberately excludes leaf stat names, which belong to their subsystem's stat-names structs,
// and extension-specific tokens.
#define WELL_KNOWN_STAT_TOKENS(TOKEN)                                                              \
  TOKEN(admin)                                                                                     \
  TOKEN(cluster)                                                                                   \
  TOKEN(cluster_manager)                                                                           \
  TOKEN(downstream)                                                                                \
  TOKEN(http)                                                                                      \
  TOKEN(listener)                                                                                  \
  TOKEN(listener_manager)                                                                          \
  TOKEN(mongo)                                                                                     \
  TOKEN(route)                                                                                     \
  TOKEN(runtime)                                                                                   \
  TOKEN(server)                                                                                    \
  TOKEN(upstream)                                                                                  \
  TOKEN(vcluster)                                                                                  \
  TOKEN(vhost)

// Well-known stat tag names, pre-encoded for the subsystems that attach tags programmatically.
// Every entry expands to the "envoy."-prefixed tag name and must match the corresponding
// constant in Config::TagNames, which cannot be referenced from here without a dependency
// cycle; a unit test keeps the strings in sync. This is the subset of tags used by core
// subsystems: extensions encode their own tag names at configuration load time instead.
#define WELL_KNOWN_STAT_TAGS(TAG)                                                                  \
  TAG(cluster_name)                                                                                \
  TAG(health_check_name)                                                                           \
  TAG(http_conn_manager_prefix)                                                                    \
  TAG(listener_address)                                                                            \
  TAG(mongo_prefix)                                                                                \
  TAG(rds_route_config)                                                                            \
  TAG(response_code)                                                                               \
  TAG(response_code_class)                                                                         \
  TAG(route)                                                                                       \
  TAG(scoped_rds_config)                                                                           \
  TAG(ssl_cipher)                                                                                  \
  TAG(ssl_version)                                                                                 \
  TAG(virtual_cluster)                                                                             \
  TAG(virtual_host)                                                                                \
  TAG(worker_id)

/**
 * Pre-encoded StatNames for the well-known tokens and tag names above, owned by and exposed from
 * the SymbolTable: wherever a scope or symbol table is available, use
 * symbolTable().wellKnownStatNames() instead of encoding these names into a local pool. Tokens
 * are exposed as <token>_ and tag names as tag_<name>_.
 */
class WellKnownStatNames {
private:
  // Owns the backing storage of the StatName members; declared first so that it is initialized
  // before them.
  StatNamePool pool_;

public:
  explicit WellKnownStatNames(SymbolTable& symbol_table);

#define GENERATE_WELL_KNOWN_TOKEN_MEMBER(name) const StatName name##_;
#define GENERATE_WELL_KNOWN_TAG_MEMBER(name) const StatName tag_##name##_;
  WELL_KNOWN_STAT_TOKENS(GENERATE_WELL_KNOWN_TOKEN_MEMBER)
  WELL_KNOWN_STAT_TAGS(GENERATE_WELL_KNOWN_TAG_MEMBER)
#undef GENERATE_WELL_KNOWN_TOKEN_MEMBER
#undef GENERATE_WELL_KNOWN_TAG_MEMBER
};

} // namespace Stats
} // namespace Envoy
