#include "source/common/http/codes.h"

#include <array>
#include <cstdint>
#include <string>

#include "envoy/http/header_map.h"
#include "envoy/stats/scope.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Http {

CodeStatsImpl::CodeStatsImpl(Stats::SymbolTable& symbol_table)
    : stat_name_pool_(symbol_table), symbol_table_(symbol_table),
      canary_(stat_name_pool_.add("canary")), external_(stat_name_pool_.add("external")),
      internal_(stat_name_pool_.add("internal")),
      upstream_rq_1xx_(stat_name_pool_.add("upstream_rq_1xx")),
      upstream_rq_2xx_(stat_name_pool_.add("upstream_rq_2xx")),
      upstream_rq_3xx_(stat_name_pool_.add("upstream_rq_3xx")),
      upstream_rq_4xx_(stat_name_pool_.add("upstream_rq_4xx")),
      upstream_rq_5xx_(stat_name_pool_.add("upstream_rq_5xx")),
      upstream_rq_unknown_(stat_name_pool_.add("upstream_rq_unknown")), // Covers invalid http
                                                                        // response codes e.g. 600.
      upstream_rq_completed_(stat_name_pool_.add("upstream_rq_completed")),
      upstream_rq_time_(stat_name_pool_.add("upstream_rq_time")),
      vcluster_(stat_name_pool_.add("vcluster")), vhost_(stat_name_pool_.add("vhost")),
      route_(stat_name_pool_.add("route")), zone_(stat_name_pool_.add("zone")) {

  // Pre-allocate response codes 200, 404, and 503, as those seem quite likely.
  // We don't pre-allocate all the HTTP codes because the first 127 allocations
  // are likely to be encoded in one byte, and we would rather spend those on
  // common components of stat-names that appear frequently.
  upstreamRqStatName(Code::OK);
  upstreamRqStatName(Code::NotFound);
  upstreamRqStatName(Code::ServiceUnavailable);
}

void CodeStatsImpl::incCounter(Stats::Scope& scope, const Stats::StatNameVec& names) const {
  const Stats::StatNameJoiner joined(names, symbol_table_);
  scope.counterFromStatName(joined.statName()).inc();
}

void CodeStatsImpl::incCounter(Stats::Scope& scope, Stats::StatName a, Stats::StatName b) const {
  const Stats::StatNameJoiner joined({a, b}, symbol_table_);
  scope.counterFromStatName(joined.statName()).inc();
}

void CodeStatsImpl::recordHistogram(Stats::Scope& scope, const Stats::StatNameVec& names,
                                    Stats::Histogram::Unit unit, uint64_t count) const {
  const Stats::StatNameJoiner joined(names, symbol_table_);
  scope.histogramFromStatName(joined.statName(), unit).recordValue(count);
}

void CodeStatsImpl::chargeBasicResponseStat(Stats::Scope& scope, Stats::StatName prefix,
                                            Code response_code,
                                            bool exclude_http_code_stats) const {
  ASSERT(&symbol_table_ == &scope.symbolTable());

  // Build a dynamic stat for the response code and increment it.
  incCounter(scope, prefix, upstream_rq_completed_);

  if (!exclude_http_code_stats) {
    const Stats::StatName rq_group = upstreamRqGroup(response_code);
    if (!rq_group.empty()) {
      incCounter(scope, prefix, rq_group);
    }
    incCounter(scope, prefix, upstreamRqStatName(response_code));
  }
}

void CodeStatsImpl::chargeResponseStat(const ResponseStatInfo& info,
                                       bool exclude_http_code_stats) const {
  const Code code = static_cast<Code>(info.response_status_code_);

  ASSERT(&info.cluster_scope_.symbolTable() == &symbol_table_);
  chargeBasicResponseStat(info.cluster_scope_, info.prefix_, code, exclude_http_code_stats);

  const Stats::StatName rq_group = upstreamRqGroup(code);
  const Stats::StatName rq_code = upstreamRqStatName(code);

  // If the response is from a canary, also create canary stats.
  if (info.upstream_canary_) {
    writeCategory(info, rq_group, rq_code, canary_);
  }

  // Split stats into external vs. internal.
  if (info.internal_request_) {
    writeCategory(info, rq_group, rq_code, internal_);
  } else {
    writeCategory(info, rq_group, rq_code, external_);
  }

  // Note: unlike writeCategory() and chargeBasicResponseStat() above, the three blocks below
  // charge `rq_group` without first checking that it is non-empty. For a response code outside
  // 1xx-5xx there is no group, and StatNameJoiner drops the empty name, so each block charges an
  // extra counter with no 'upstream_rq*' leaf at all: 'vhost.<vhost>.vcluster.<vcluster>',
  // 'vhost.<vhost>.route.<route>' and '<prefix>.zone.<from>.<to>'. These are the only stats
  // TaggedCodeStatsImpl does not reproduce.

  // Handle request virtual cluster.
  if (!info.request_vcluster_name_.empty()) {
    incCounter(info.global_scope_, {vhost_, info.request_vhost_name_, vcluster_,
                                    info.request_vcluster_name_, upstream_rq_completed_});
    incCounter(info.global_scope_, {vhost_, info.request_vhost_name_, vcluster_,
                                    info.request_vcluster_name_, rq_group});
    incCounter(info.global_scope_,
               {vhost_, info.request_vhost_name_, vcluster_, info.request_vcluster_name_, rq_code});
  }

  // Handle route level stats.
  if (!info.request_route_name_.empty()) {
    incCounter(info.global_scope_, {vhost_, info.request_vhost_name_, route_,
                                    info.request_route_name_, upstream_rq_completed_});
    incCounter(info.global_scope_,
               {vhost_, info.request_vhost_name_, route_, info.request_route_name_, rq_group});
    incCounter(info.global_scope_,
               {vhost_, info.request_vhost_name_, route_, info.request_route_name_, rq_code});
  }

  // Handle per zone stats.
  if (!info.from_zone_.empty() && !info.to_zone_.empty()) {
    incCounter(info.cluster_scope_,
               {info.prefix_, zone_, info.from_zone_, info.to_zone_, upstream_rq_completed_});
    incCounter(info.cluster_scope_,
               {info.prefix_, zone_, info.from_zone_, info.to_zone_, rq_group});
    incCounter(info.cluster_scope_, {info.prefix_, zone_, info.from_zone_, info.to_zone_, rq_code});
  }
}

void CodeStatsImpl::writeCategory(const ResponseStatInfo& info, Stats::StatName rq_group,
                                  Stats::StatName rq_code, Stats::StatName category) const {
  incCounter(info.cluster_scope_, {info.prefix_, category, upstream_rq_completed_});
  if (!rq_group.empty()) {
    incCounter(info.cluster_scope_, {info.prefix_, category, rq_group});
  }
  incCounter(info.cluster_scope_, {info.prefix_, category, rq_code});
}

void CodeStatsImpl::chargeResponseTiming(const ResponseTimingInfo& info) const {
  const uint64_t count = info.response_time_.count();
  recordHistogram(info.cluster_scope_, {info.prefix_, upstream_rq_time_},
                  Stats::Histogram::Unit::Milliseconds, count);
  if (info.upstream_canary_) {
    recordHistogram(info.cluster_scope_, {info.prefix_, canary_, upstream_rq_time_},
                    Stats::Histogram::Unit::Milliseconds, count);
  }

  if (info.internal_request_) {
    recordHistogram(info.cluster_scope_, {info.prefix_, internal_, upstream_rq_time_},
                    Stats::Histogram::Unit::Milliseconds, count);
  } else {
    recordHistogram(info.cluster_scope_, {info.prefix_, external_, upstream_rq_time_},
                    Stats::Histogram::Unit::Milliseconds, count);
  }

  if (!info.request_vcluster_name_.empty()) {
    recordHistogram(info.global_scope_,
                    {vhost_, info.request_vhost_name_, vcluster_, info.request_vcluster_name_,
                     upstream_rq_time_},
                    Stats::Histogram::Unit::Milliseconds, count);
  }

  if (!info.request_route_name_.empty()) {
    recordHistogram(
        info.global_scope_,
        {vhost_, info.request_vhost_name_, route_, info.request_route_name_, upstream_rq_time_},
        Stats::Histogram::Unit::Milliseconds, count);
  }

  // Handle per zone stats.
  if (!info.from_zone_.empty() && !info.to_zone_.empty()) {
    recordHistogram(info.cluster_scope_,
                    {info.prefix_, zone_, info.from_zone_, info.to_zone_, upstream_rq_time_},
                    Stats::Histogram::Unit::Milliseconds, count);
  }
}

Stats::StatName CodeStatsImpl::upstreamRqGroup(Code response_code) const {
  switch (enumToInt(response_code) / 100) {
  case 1:
    return upstream_rq_1xx_;
  case 2:
    return upstream_rq_2xx_;
  case 3:
    return upstream_rq_3xx_;
  case 4:
    return upstream_rq_4xx_;
  case 5:
    return upstream_rq_5xx_;
  }
  return empty_; // Unknown codes do not go into a group.
}

Stats::StatName CodeStatsImpl::upstreamRqStatName(Code response_code) const {
  // Take a lock only if we've never seen this response-code before.
  const uint32_t rc_index = static_cast<uint32_t>(response_code) - HttpCodeOffset;
  if (rc_index >= NumHttpCodes) {
    return upstream_rq_unknown_;
  }
  return Stats::StatName(rc_stat_names_.get(rc_index, [this, response_code]() -> const uint8_t* {
    return stat_name_pool_.addReturningStorage(
        absl::StrCat("upstream_rq_", enumToInt(response_code)));
  }));
}

TaggedCodeStatsImpl::StatNamesBase::StatNamesBase(Stats::SymbolTable& symbol_table,
                                                  absl::string_view prefix)
    : pool_(symbol_table), prefix_(prefix.empty() ? std::string() : absl::StrCat(prefix, ".")),
      upstream_rq_(pool_.add(absl::StrCat(prefix_, "upstream_rq"))),
      upstream_rq_xx_(pool_.add(absl::StrCat(prefix_, "upstream_rq_xx"))),
      completed_(pool_.add(absl::StrCat(prefix_, "upstream_rq_completed"))),
      time_(pool_.add(absl::StrCat(prefix_, "upstream_rq_time"))),
      unknown_(pool_.add(absl::StrCat(prefix_, "upstream_rq_unknown"))) {}

TaggedCodeStatsImpl::ResponseCodeStatNames::ResponseCodeStatNames(
    Stats::SymbolTable& symbol_table, Stats::StatName response_code_tag,
    Stats::StatName response_code_class_tag, absl::string_view prefix)
    : StatNamesBase(symbol_table, prefix), response_code_tag_(response_code_tag) {
  // The class is the digit before the trailing 'xx', which is exactly what the '_rq_((\d))xx$'
  // extraction rule pulls out, leaving 'upstream_rq_xx' as the tag-extracted name.
  for (uint32_t i = 0; i < NumResponseCodeClasses; ++i) {
    const uint32_t response_code_class = i + 1;
    classes_[i] =
        CodeStatName(upstream_rq_xx_,
                     pool_.add(absl::StrCat(prefix_, "upstream_rq_", response_code_class, "xx")),
                     {response_code_class_tag, pool_.add(absl::StrCat(response_code_class))});
  }
}

TaggedCodeStatsImpl::CodeStatName
TaggedCodeStatsImpl::ResponseCodeStatNames::statusClass(Code response_code) const {
  const uint32_t response_code_class = enumToInt(response_code) / 100;
  if (response_code_class < 1 || response_code_class > NumResponseCodeClasses) {
    return {}; // Unknown codes do not go into a group.
  }
  return classes_[response_code_class - 1];
}

TaggedCodeStatsImpl::CodeStatName
TaggedCodeStatsImpl::ResponseCodeStatNames::statusCode(Code response_code) const {
  // Take a lock only if we've never seen this response-code before. The name and the tag value it
  // carries are allocated together, so that the pool is only ever mutated under the array's lock.
  const uint32_t rc_index = static_cast<uint32_t>(response_code) - HttpCodeOffset;
  if (rc_index >= NumHttpCodes) {
    return unknown_;
  }
  return *rc_stat_names_.get(rc_index, [this, response_code]() -> const CodeStatName* {
    const uint32_t code = enumToInt(response_code);
    // The '_rq(_(\d{3}))$' extraction rule removes the code along with the underscore before
    // it, leaving 'upstream_rq' as the tag-extracted name.
    return new CodeStatName(upstream_rq_, pool_.add(absl::StrCat(prefix_, "upstream_rq_", code)),
                            {response_code_tag_, pool_.add(absl::StrCat(code))});
  });
}

TaggedCodeStatsImpl::TaggedCodeStatsImpl(Stats::SymbolTable& symbol_table)
    : stat_name_pool_(symbol_table), symbol_table_(symbol_table),
      response_code_tag_(stat_name_pool_.add(Config::TagNames::get().RESPONSE_CODE)),
      response_code_class_tag_(stat_name_pool_.add(Config::TagNames::get().RESPONSE_CODE_CLASS)),
      virtual_host_tag_(stat_name_pool_.add(Config::TagNames::get().VIRTUAL_HOST)),
      virtual_cluster_tag_(stat_name_pool_.add(Config::TagNames::get().VIRTUAL_CLUSTER)),
      route_tag_(stat_name_pool_.add(Config::TagNames::get().ROUTE)),
      basic_rq_names_(symbol_table, response_code_tag_, response_code_class_tag_, ""),
      canary_rq_names_(symbol_table, response_code_tag_, response_code_class_tag_, "canary"),
      external_rq_names_(symbol_table, response_code_tag_, response_code_class_tag_, "external"),
      internal_rq_names_(symbol_table, response_code_tag_, response_code_class_tag_, "internal"),
      vhost_vcluster_names_(symbol_table, "vhost.vcluster"),
      vhost_route_names_(symbol_table, "vhost.route"), vcluster_(stat_name_pool_.add("vcluster")),
      vhost_(stat_name_pool_.add("vhost")), route_(stat_name_pool_.add("route")),
      zone_(stat_name_pool_.add("zone")) {

  // Pre-allocate response codes 200, 404, and 503, as those seem quite likely.
  // We don't pre-allocate all the HTTP codes because the first 127 allocations
  // are likely to be encoded in one byte, and we would rather spend those on
  // common components of stat-names that appear frequently. Note the names are
  // encoded token by token, so the categories above share the token of the code
  // with the names pre-allocated here.
  basic_rq_names_.statusCode(Code::OK);
  basic_rq_names_.statusCode(Code::NotFound);
  basic_rq_names_.statusCode(Code::ServiceUnavailable);
}

void TaggedCodeStatsImpl::incCounter(Stats::Scope& scope,
                                     absl::Span<const Stats::StatName> names) const {
  const Stats::StatNameJoiner joined(names, symbol_table_);
  scope.counterFromStatName(joined.statName()).inc();
}

void TaggedCodeStatsImpl::incCounter(Stats::Scope& scope, Stats::StatName prefix,
                                     CodeStatName leaf) const {
  incCounter(scope, {prefix, leaf.base_name_},
             leaf.tag_.first.empty() ? Stats::StatNameTagSpan{} : Stats::StatNameTagSpan{leaf.tag_},
             {prefix, leaf.name_});
}

void TaggedCodeStatsImpl::incCounter(Stats::Scope& scope,
                                     absl::Span<const Stats::StatName> base_names,
                                     Stats::StatNameTagSpan tags,
                                     absl::Span<const Stats::StatName> names) const {
  if (tags.empty()) {
    incCounter(scope, names);
    return;
  }
  const Stats::StatNameJoiner base(base_names, symbol_table_);
  const Stats::StatNameJoiner joined(names, symbol_table_);
  scope.counterFromTaggedName(base.statName(), tags, joined.statName()).inc();
}

void TaggedCodeStatsImpl::incCounter(Stats::Scope& scope, Stats::StatName base_name,
                                     Stats::StatNameTagSpan tags,
                                     absl::Span<const Stats::StatName> names) const {
  if (tags.empty()) {
    incCounter(scope, names);
    return;
  }
  const Stats::StatNameJoiner joined(names, symbol_table_);
  scope.counterFromTaggedName(base_name, tags, joined.statName()).inc();
}

void TaggedCodeStatsImpl::recordHistogram(Stats::Scope& scope,
                                          absl::Span<const Stats::StatName> names,
                                          Stats::Histogram::Unit unit, uint64_t count) const {
  const Stats::StatNameJoiner joined(names, symbol_table_);
  scope.histogramFromStatName(joined.statName(), unit).recordValue(count);
}

void TaggedCodeStatsImpl::recordHistogram(Stats::Scope& scope, Stats::StatName base_name,
                                          Stats::StatNameTagSpan tags,
                                          absl::Span<const Stats::StatName> names,
                                          Stats::Histogram::Unit unit, uint64_t count) const {
  if (tags.empty()) {
    recordHistogram(scope, names, unit, count);
    return;
  }
  const Stats::StatNameJoiner joined(names, symbol_table_);
  scope.histogramFromTaggedName(base_name, tags, joined.statName(), unit).recordValue(count);
}

void TaggedCodeStatsImpl::writeVhostVcluster(const ResponseStatInfo& info, Stats::StatName base,
                                             CodeStatName leaf) const {
  incCounter(
      info.global_scope_, base,
      Stats::StatNameTagSpan{{virtual_host_tag_, info.request_vhost_name_},
                             {virtual_cluster_tag_, info.request_vcluster_name_},
                             leaf.tag_}
          .subspan(0, leaf.tag_.first.empty() ? 2 : 3),
      {vhost_, info.request_vhost_name_, vcluster_, info.request_vcluster_name_, leaf.name_});
}

void TaggedCodeStatsImpl::writeVhostRoute(const ResponseStatInfo& info, Stats::StatName base,
                                          CodeStatName leaf) const {
  incCounter(info.global_scope_, base,
             Stats::StatNameTagSpan{{virtual_host_tag_, info.request_vhost_name_},
                                    {route_tag_, info.request_route_name_},
                                    leaf.tag_}
                 .subspan(0, leaf.tag_.first.empty() ? 2 : 3),
             {vhost_, info.request_vhost_name_, route_, info.request_route_name_, leaf.name_});
}

void TaggedCodeStatsImpl::writeUpstreamZone(const ResponseStatInfo& info, CodeStatName leaf) const {
  incCounter(info.cluster_scope_,
             {info.prefix_, zone_, info.from_zone_, info.to_zone_, leaf.base_name_},
             Stats::StatNameTagSpan{leaf.tag_}.subspan(0, leaf.tag_.first.empty() ? 0 : 1),
             {info.prefix_, zone_, info.from_zone_, info.to_zone_, leaf.name_});
}

void TaggedCodeStatsImpl::chargeBasicResponseStat(Stats::Scope& scope, Stats::StatName prefix,
                                                  Code response_code,
                                                  bool exclude_http_code_stats) const {
  ASSERT(&symbol_table_ == &scope.symbolTable());

  // Build a dynamic stat for the response code and increment it.
  incCounter(scope, {prefix, basic_rq_names_.completed()});

  if (!exclude_http_code_stats) {
    const CodeStatName rq_group = basic_rq_names_.statusClass(response_code);
    if (!rq_group.empty()) {
      incCounter(scope, prefix, rq_group);
    }
    incCounter(scope, prefix, basic_rq_names_.statusCode(response_code));
  }
}

void TaggedCodeStatsImpl::chargeResponseStat(const ResponseStatInfo& info,
                                             bool exclude_http_code_stats) const {
  const Code code = static_cast<Code>(info.response_status_code_);

  ASSERT(&info.cluster_scope_.symbolTable() == &symbol_table_);
  chargeBasicResponseStat(info.cluster_scope_, info.prefix_, code, exclude_http_code_stats);

  const CodeStatName rq_group = basic_rq_names_.statusClass(code);
  const CodeStatName rq_code = basic_rq_names_.statusCode(code);
  // A response code outside 1xx-5xx goes into no class, and has no name of its own either: its
  // class stat has no leaf name at all, and its own stat is the untagged 'upstream_rq_unknown'.
  const bool has_group = !rq_group.empty();

  // If the response is from a canary, also create canary stats.
  if (info.upstream_canary_) {
    writeCategory(info, code, canary_rq_names_);
  }

  // Split stats into external vs. internal.
  writeCategory(info, code, info.internal_request_ ? internal_rq_names_ : external_rq_names_);

  // Handle request virtual cluster.
  if (!info.request_vcluster_name_.empty()) {
    // vhost.[<vhost>.]vcluster.[<vcluster>.]upstream_rq*
    writeVhostVcluster(info, vhost_vcluster_names_.completed_, basic_rq_names_.completed());
    if (has_group) {
      writeVhostVcluster(info, vhost_vcluster_names_.upstream_rq_xx_, rq_group);
      writeVhostVcluster(info, vhost_vcluster_names_.upstream_rq_, rq_code);
    } else {
      writeVhostVcluster(info, vhost_vcluster_names_.unknown_, basic_rq_names_.unknown());
    }
  }

  // Handle route level stats.
  if (!info.request_route_name_.empty()) {
    // vhost.[<vhost>.]route.[<route>.]upstream_rq*
    writeVhostRoute(info, vhost_route_names_.completed_, basic_rq_names_.completed());
    if (has_group) {
      writeVhostRoute(info, vhost_route_names_.upstream_rq_xx_, rq_group);
      writeVhostRoute(info, vhost_route_names_.upstream_rq_, rq_code);
    } else {
      writeVhostRoute(info, vhost_route_names_.unknown_, basic_rq_names_.unknown());
    }
  }

  // Handle per zone stats. The zones are part of the stat name; they carry no tags of their own.
  if (!info.from_zone_.empty() && !info.to_zone_.empty()) {
    writeUpstreamZone(info, basic_rq_names_.completed());
    if (has_group) {
      writeUpstreamZone(info, rq_group);
      writeUpstreamZone(info, rq_code);
    } else {
      writeUpstreamZone(info, basic_rq_names_.unknown());
    }
  }
}

void TaggedCodeStatsImpl::writeCategory(const ResponseStatInfo& info, Code response_code,
                                        const ResponseCodeStatNames& rq_names) const {
  incCounter(info.cluster_scope_, info.prefix_, rq_names.completed());
  const CodeStatName rq_group = rq_names.statusClass(response_code);
  if (!rq_group.empty()) {
    incCounter(info.cluster_scope_, info.prefix_, rq_group);
  }
  incCounter(info.cluster_scope_, info.prefix_, rq_names.statusCode(response_code));
}

void TaggedCodeStatsImpl::chargeResponseTiming(const ResponseTimingInfo& info) const {
  const uint64_t count = info.response_time_.count();
  recordHistogram(info.cluster_scope_, {info.prefix_, basic_rq_names_.time()},
                  Stats::Histogram::Unit::Milliseconds, count);
  if (info.upstream_canary_) {
    recordHistogram(info.cluster_scope_, {info.prefix_, canary_rq_names_.time()},
                    Stats::Histogram::Unit::Milliseconds, count);
  }

  if (info.internal_request_) {
    recordHistogram(info.cluster_scope_, {info.prefix_, internal_rq_names_.time()},
                    Stats::Histogram::Unit::Milliseconds, count);
  } else {
    recordHistogram(info.cluster_scope_, {info.prefix_, external_rq_names_.time()},
                    Stats::Histogram::Unit::Milliseconds, count);
  }

  if (!info.request_vcluster_name_.empty()) {
    // vhost.[<vhost>.]vcluster.[<vcluster>.]upstream_rq_time
    recordHistogram(info.global_scope_, vhost_vcluster_names_.time_,
                    {{virtual_host_tag_, info.request_vhost_name_},
                     {virtual_cluster_tag_, info.request_vcluster_name_}},
                    {vhost_, info.request_vhost_name_, vcluster_, info.request_vcluster_name_,
                     basic_rq_names_.time()},
                    Stats::Histogram::Unit::Milliseconds, count);
  }

  if (!info.request_route_name_.empty()) {
    // vhost.[<vhost>.]route.[<route>.]upstream_rq_time
    recordHistogram(
        info.global_scope_, vhost_route_names_.time_,
        {{virtual_host_tag_, info.request_vhost_name_}, {route_tag_, info.request_route_name_}},
        {vhost_, info.request_vhost_name_, route_, info.request_route_name_,
         basic_rq_names_.time()},
        Stats::Histogram::Unit::Milliseconds, count);
  }

  // Handle per zone stats. The zones are part of the stat name; they carry no tags of their own.
  if (!info.from_zone_.empty() && !info.to_zone_.empty()) {
    recordHistogram(info.cluster_scope_,
                    {info.prefix_, zone_, info.from_zone_, info.to_zone_, basic_rq_names_.time()},
                    Stats::Histogram::Unit::Milliseconds, count);
  }
}

std::string CodeUtility::groupStringForResponseCode(Code response_code) {
  // Note: this is only used in the unit test and in dynamo_filter.cc, which
  // needs the same sort of symbolization treatment we are doing here.
  if (CodeUtility::is1xx(enumToInt(response_code))) {
    return "1xx";
  } else if (CodeUtility::is2xx(enumToInt(response_code))) {
    return "2xx";
  } else if (CodeUtility::is3xx(enumToInt(response_code))) {
    return "3xx";
  } else if (CodeUtility::is4xx(enumToInt(response_code))) {
    return "4xx";
  } else if (CodeUtility::is5xx(enumToInt(response_code))) {
    return "5xx";
  } else {
    return "";
  }
}

const char* CodeUtility::toString(Code code) {
  // clang-format off
  switch (code) {
  // 1xx
  case Code::Continue:                      return "Continue";
  case Code::SwitchingProtocols:            return "Switching Protocols";

  // 2xx
  case Code::OK:                            return "OK";
  case Code::Created:                       return "Created";
  case Code::Accepted:                      return "Accepted";
  case Code::NonAuthoritativeInformation:   return "Non-Authoritative Information";
  case Code::NoContent:                     return "No Content";
  case Code::ResetContent:                  return "Reset Content";
  case Code::PartialContent:                return "Partial Content";
  case Code::MultiStatus:                   return "Multi-Status";
  case Code::AlreadyReported:               return "Already Reported";
  case Code::IMUsed:                        return "IM Used";

  // 3xx
  case Code::MultipleChoices:               return "Multiple Choices";
  case Code::MovedPermanently:              return "Moved Permanently";
  case Code::Found:                         return "Found";
  case Code::SeeOther:                      return "See Other";
  case Code::NotModified:                   return "Not Modified";
  case Code::UseProxy:                      return "Use Proxy";
  case Code::TemporaryRedirect:             return "Temporary Redirect";
  case Code::PermanentRedirect:             return "Permanent Redirect";

  // 4xx
  case Code::BadRequest:                    return "Bad Request";
  case Code::Unauthorized:                  return "Unauthorized";
  case Code::PaymentRequired:               return "Payment Required";
  case Code::Forbidden:                     return "Forbidden";
  case Code::NotFound:                      return "Not Found";
  case Code::MethodNotAllowed:              return "Method Not Allowed";
  case Code::NotAcceptable:                 return "Not Acceptable";
  case Code::ProxyAuthenticationRequired:   return "Proxy Authentication Required";
  case Code::RequestTimeout:                return "Request Timeout";
  case Code::Conflict:                      return "Conflict";
  case Code::Gone:                          return "Gone";
  case Code::LengthRequired:                return "Length Required";
  case Code::PreconditionFailed:            return "Precondition Failed";
  case Code::PayloadTooLarge:               return "Payload Too Large";
  case Code::URITooLong:                    return "URI Too Long";
  case Code::UnsupportedMediaType:          return "Unsupported Media Type";
  case Code::RangeNotSatisfiable:           return "Range Not Satisfiable";
  case Code::ExpectationFailed:             return "Expectation Failed";
  case Code::MisdirectedRequest:            return "Misdirected Request";
  case Code::UnprocessableEntity:           return "Unprocessable Entity";
  case Code::Locked:                        return "Locked";
  case Code::FailedDependency:              return "Failed Dependency";
  case Code::UpgradeRequired:               return "Upgrade Required";
  case Code::PreconditionRequired:          return "Precondition Required";
  case Code::TooManyRequests:               return "Too Many Requests";
  case Code::RequestHeaderFieldsTooLarge:   return "Request Header Fields Too Large";
  case Code::TooEarly:                      return "Too Early";

  // 5xx
  case Code::InternalServerError:           return "Internal Server Error";
  case Code::NotImplemented:                return "Not Implemented";
  case Code::BadGateway:                    return "Bad Gateway";
  case Code::ServiceUnavailable:            return "Service Unavailable";
  case Code::GatewayTimeout:                return "Gateway Timeout";
  case Code::HTTPVersionNotSupported:       return "HTTP Version Not Supported";
  case Code::VariantAlsoNegotiates:         return "Variant Also Negotiates";
  case Code::InsufficientStorage:           return "Insufficient Storage";
  case Code::LoopDetected:                  return "Loop Detected";
  case Code::NotExtended:                   return "Not Extended";
  case Code::NetworkAuthenticationRequired: return "Network Authentication Required";
  case Code::LastUnassignedServerErrorCode: return "Last Unassigned Server Error Code";
  }
  // clang-format on

  return "Unknown";
}

} // namespace Http
} // namespace Envoy
