#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/scope.h"

#include "source/common/common/thread.h"
#include "source/common/stats/symbol_table.h"

#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Http {

struct CodeStats::ResponseStatInfo {
  Stats::Scope& global_scope_;
  Stats::Scope& cluster_scope_;
  Stats::StatName prefix_;
  uint64_t response_status_code_;
  bool internal_request_;
  Stats::StatName request_vhost_name_;
  Stats::StatName request_vcluster_name_;
  Stats::StatName request_route_name_;
  Stats::StatName from_zone_;
  Stats::StatName to_zone_;
  bool upstream_canary_;
};

struct CodeStats::ResponseTimingInfo {
  Stats::Scope& global_scope_;
  Stats::Scope& cluster_scope_;
  Stats::StatName prefix_;
  std::chrono::milliseconds response_time_;
  bool upstream_canary_;
  bool internal_request_;
  Stats::StatName request_vhost_name_;
  Stats::StatName request_vcluster_name_;
  Stats::StatName request_route_name_;
  Stats::StatName from_zone_;
  Stats::StatName to_zone_;
};

class CodeStatsImpl : public CodeStats {
public:
  explicit CodeStatsImpl(Stats::SymbolTable& symbol_table);

  // CodeStats
  void chargeBasicResponseStat(Stats::Scope& scope, Stats::StatName prefix, Code response_code,
                               bool exclude_http_code_stats) const override;
  void chargeResponseStat(const ResponseStatInfo& info,
                          bool exclude_http_code_stats) const override;
  void chargeResponseTiming(const ResponseTimingInfo& info) const override;

private:
  friend class CodeStatsTest;

  void writeCategory(const ResponseStatInfo& info, Stats::StatName rq_group,
                     Stats::StatName rq_code, Stats::StatName category) const;
  void incCounter(Stats::Scope& scope, const Stats::StatNameVec& names) const;
  void incCounter(Stats::Scope& scope, Stats::StatName a, Stats::StatName b) const;
  void recordHistogram(Stats::Scope& scope, const Stats::StatNameVec& names,
                       Stats::Histogram::Unit unit, uint64_t count) const;

  Stats::StatName upstreamRqGroup(Code response_code) const;
  Stats::StatName upstreamRqStatName(Code response_code) const;

  mutable Stats::StatNamePool stat_name_pool_;
  Stats::SymbolTable& symbol_table_;

  const Stats::StatName canary_;
  const Stats::StatName empty_; // Used for the group-name for invalid http codes.
  const Stats::StatName external_;
  const Stats::StatName internal_;
  const Stats::StatName upstream_;
  const Stats::StatName upstream_rq_1xx_;
  const Stats::StatName upstream_rq_2xx_;
  const Stats::StatName upstream_rq_3xx_;
  const Stats::StatName upstream_rq_4xx_;
  const Stats::StatName upstream_rq_5xx_;
  const Stats::StatName upstream_rq_unknown_;
  const Stats::StatName upstream_rq_completed_;
  const Stats::StatName upstream_rq_time_;
  const Stats::StatName vcluster_;
  const Stats::StatName vhost_;
  const Stats::StatName route_;
  const Stats::StatName zone_;

  // Use an array of atomic pointers to hold StatNameStorage objects for
  // every conceivable HTTP response code. In the hot-path we'll reference
  // these with a null-check, and if we need to allocate a symbol for a
  // new code, we'll take a mutex to avoid duplicate allocations and
  // subsequent leaks. This is similar in principle to a ReaderMutexLock,
  // but should be faster, as ReaderMutexLocks appear to be too expensive for
  // fine-grained controls. Another option would be to use a lock per
  // stat-name, which might have similar performance to atomics with default
  // barrier policy.
  //
  // We don't allocate these all up front during construction because
  // SymbolTable greedily encodes the first 128 names it discovers in one
  // byte. We don't want those high-value single-byte codes to go to fully
  // enumerating the 4 prefixes combined with HTTP codes that are seldom used,
  // so we allocate these on demand.
  //
  // There can be multiple symbol tables in a server. The one passed into the
  // Codes constructor should be the same as the one passed to
  // Stats::ThreadLocalStore. Note that additional symbol tables can be created
  // from IsolatedStoreImpl's default constructor.
  //
  // The Codes object is global to the server.

  static constexpr uint32_t NumHttpCodes = 500;
  static constexpr uint32_t HttpCodeOffset = 100; // code 100 is at index 0.
  mutable Thread::AtomicPtrArray<const uint8_t, NumHttpCodes,
                                 Thread::AtomicPtrAllocMode::DoNotDelete>
      rc_stat_names_;
};

class TaggedCodeStatsImpl : public CodeStats {
public:
  explicit TaggedCodeStatsImpl(Stats::SymbolTable& symbol_table);

  // CodeStats
  void chargeBasicResponseStat(Stats::Scope& scope, Stats::StatName prefix, Code response_code,
                               bool exclude_http_code_stats) const override;
  void chargeResponseStat(const ResponseStatInfo& info,
                          bool exclude_http_code_stats) const override;
  void chargeResponseTiming(const ResponseTimingInfo& info) const override;

private:
  /**
   * The tag-extracted ('base') names of the 'upstream_rq*' stats under one naming context: a
   * category such as 'canary.', or a context relative to the scope such as 'vhost.vcluster.'.
   */
  class StatNamesBase {
  public:
    /**
     * @param symbol_table the symbol table to allocate the names from.
     * @param prefix the context the names sit under, without a trailing dot, or empty for names
     * charged directly under the scope, e.g. 'vhost.vcluster' or 'canary'.
     */
    StatNamesBase(Stats::SymbolTable& symbol_table, absl::string_view prefix);

  protected:
    // Declared ahead of the names so that they can be allocated from it. A derived class
    // allocates the names it adds from this pool too.
    mutable Stats::StatNamePool pool_;
    const std::string prefix_; // '<prefix>.', or empty when there is no context.
  public:
    // '<prefix>upstream_rq', the base name of a specific response code's stat.
    Stats::StatName upstream_rq_;
    // '<prefix>upstream_rq_xx', the base name of a response code class's stat.
    Stats::StatName upstream_rq_xx_;
    Stats::StatName completed_;
    Stats::StatName time_;
    Stats::StatName unknown_; // Covers invalid http response codes e.g. 600.
  };

  /**
   * A stat name that may carry a tag value within it, such as 'upstream_rq_2xx'. `name_` is the
   * flat name, `base_name_` is the same name with the tag value removed, which is what the tag
   * extraction rules produce, and `tag_` is the tag itself.
   */
  struct CodeStatName {
    CodeStatName(Stats::StatName name = {}) : base_name_(name), name_(name) {}
    CodeStatName(Stats::StatName base_name, Stats::StatName name, Stats::StatNameTag tag)
        : base_name_(base_name), name_(name), tag_(tag) {}

    bool empty() const { return name_.empty(); }

    Stats::StatName base_name_;
    Stats::StatName name_;
    Stats::StatNameTag tag_;
  };

  /**
   * All the 'upstream_rq*' stat names, optionally prefixed by the category they are charged
   * under, such as 'canary'. The prefix is part of every name held here, so that the names of a
   * category are ready to use on the request path rather than having to be joined with the
   * category token there.
   */
  class ResponseCodeStatNames : public StatNamesBase {
  public:
    /**
     * @param symbol_table the symbol table to allocate the names from.
     * @param response_code_tag the name of the tag that a response code's name carries.
     * @param response_code_class_tag the name of the tag that a code class's name carries. The
     * tag names are the same for every category, so the owner allocates them and passes them in
     * rather than every category allocating its own.
     * @param prefix the category the names are prefixed with, without a trailing dot,
     * e.g. 'canary' for the 'canary.upstream_rq*' names.
     */
    ResponseCodeStatNames(Stats::SymbolTable& symbol_table, Stats::StatName response_code_tag,
                          Stats::StatName response_code_class_tag, absl::string_view prefix);

    Stats::StatName completed() const { return completed_; }
    Stats::StatName time() const { return time_; }
    Stats::StatName unknown() const { return unknown_; }

    /**
     * @return the '[<prefix>.]upstream_rq_<response_code_class>xx' name, which carries the class
     * as a tag, or an empty name for a response code outside 1xx-5xx, as those go into no class.
     */
    CodeStatName statusClass(Code response_code) const;

    /**
     * @return the '[<prefix>.]upstream_rq_<response_code>' name, which carries the code as a tag,
     * or the untagged '[<prefix>.]upstream_rq_unknown' for a response code that has no name of its
     * own.
     */
    CodeStatName statusCode(Code response_code) const;

  private:
    static constexpr uint32_t NumHttpCodes = 500;
    static constexpr uint32_t HttpCodeOffset = 100;       // code 100 is at index 0.
    static constexpr uint32_t NumResponseCodeClasses = 5; // 1xx through 5xx.

    // The name of the tag a response code's name carries, needed by the names that are allocated
    // on demand below. The class tag is not kept: the class names are all allocated at
    // construction, which bakes it into each of them.
    const Stats::StatName response_code_tag_;
    std::array<CodeStatName, NumResponseCodeClasses> classes_;

    // Use an array of atomic pointers to hold the name of every conceivable
    // HTTP response code. In the hot-path we'll reference these with a
    // null-check, and if we need to allocate a symbol for a new code, we'll
    // take a mutex to avoid duplicate allocations and subsequent leaks. This is
    // similar in principle to a ReaderMutexLock, but should be faster, as
    // ReaderMutexLocks appear to be too expensive for fine-grained controls.
    // Another option would be to use a lock per stat-name, which might have
    // similar performance to atomics with default barrier policy.
    //
    // We don't allocate these all up front during construction because
    // SymbolTable greedily encodes the first 128 names it discovers in one
    // byte. We don't want those high-value single-byte codes to go to fully
    // enumerating the 4 prefixes combined with HTTP codes that are seldom used,
    // so we allocate these on demand.
    //
    // There can be multiple symbol tables in a server. The one passed into the
    // Codes constructor should be the same as the one passed to
    // Stats::ThreadLocalStore. Note that additional symbol tables can be created
    // from IsolatedStoreImpl's default constructor.
    //
    // The Codes object is global to the server.
    mutable Thread::AtomicPtrArray<const CodeStatName, NumHttpCodes,
                                   Thread::AtomicPtrAllocMode::DeleteOnDestruct>
        rc_stat_names_;
  };

  void writeCategory(const ResponseStatInfo& info, Code response_code,
                     const ResponseCodeStatNames& rq_names) const;
  // `base` holds the pre-joined base names of the 'vhost.vcluster.' / 'vhost.route.' context, so
  // that charging these stats joins no base name on the request path.
  void writeVhostVcluster(const ResponseStatInfo& info, Stats::StatName base,
                          CodeStatName leaf) const;
  void writeVhostRoute(const ResponseStatInfo& info, Stats::StatName base, CodeStatName leaf) const;
  void writeUpstreamZone(const ResponseStatInfo& info, CodeStatName stat_name) const;
  // The name spans below are only read while the stat is created, so call-sites can pass a braced
  // list of the name's tokens directly rather than materializing a vector of them.
  void incCounter(Stats::Scope& scope, absl::Span<const Stats::StatName> names) const;
  // Increments the '<prefix>.upstream_rq*' counter, with the tag the name carries attached
  // explicitly.
  void incCounter(Stats::Scope& scope, Stats::StatName prefix, CodeStatName leaf) const;
  // Increments the counter that `names` names, telling the scope which tags the name carries
  // rather than leaving them to be recovered from the name by the tag extraction rules.
  // `base_names` is the same name with the tag values removed. With no tags this is equivalent to
  // the overload above taking `names` alone.
  void incCounter(Stats::Scope& scope, absl::Span<const Stats::StatName> base_names,
                  Stats::StatNameTagSpan tags, absl::Span<const Stats::StatName> names) const;
  // As above, for a base name that is a single name already and so needs no join.
  void incCounter(Stats::Scope& scope, Stats::StatName base_name, Stats::StatNameTagSpan tags,
                  absl::Span<const Stats::StatName> names) const;
  void recordHistogram(Stats::Scope& scope, absl::Span<const Stats::StatName> names,
                       Stats::Histogram::Unit unit, uint64_t count) const;
  // See the tagged incCounter() above.
  void recordHistogram(Stats::Scope& scope, Stats::StatName base_name, Stats::StatNameTagSpan tags,
                       absl::Span<const Stats::StatName> names, Stats::Histogram::Unit unit,
                       uint64_t count) const;

  mutable Stats::StatNamePool stat_name_pool_;
  Stats::SymbolTable& symbol_table_;

  // The names of the tags.
  const Stats::StatName response_code_tag_;
  const Stats::StatName response_code_class_tag_;
  const Stats::StatName virtual_host_tag_;
  const Stats::StatName virtual_cluster_tag_;
  const Stats::StatName route_tag_;

  // The 'upstream_rq*' names, and the same names under each of the categories they are also
  // charged under.
  const ResponseCodeStatNames basic_rq_names_;
  const ResponseCodeStatNames canary_rq_names_;
  const ResponseCodeStatNames external_rq_names_;
  const ResponseCodeStatNames internal_rq_names_;
  // The base names of the two contexts the vhost stats are charged under, joined once here so
  // that the request path can name them directly.
  const StatNamesBase vhost_vcluster_names_;
  const StatNamesBase vhost_route_names_;

  const Stats::StatName vcluster_;
  const Stats::StatName vhost_;
  const Stats::StatName route_;
  const Stats::StatName zone_;
};

/**
 * General utility routines for HTTP codes.
 */
class CodeUtility {
public:
  /**
   * Convert an HTTP response code to a descriptive string.
   * @param code supplies the code to convert.
   * @return const char* the string.
   */
  static const char* toString(Code code);

  static bool is1xx(uint64_t code) { return code >= 100 && code < 200; }
  static bool is2xx(uint64_t code) { return code >= 200 && code < 300; }
  static bool is3xx(uint64_t code) { return code >= 300 && code < 400; }
  static bool is4xx(uint64_t code) { return code >= 400 && code < 500; }
  static bool is5xx(uint64_t code) { return code >= 500 && code < 600; }

  static bool isGatewayError(uint64_t code) { return code >= 502 && code < 505; }

  static std::string groupStringForResponseCode(Code response_code);
};

} // namespace Http
} // namespace Envoy
