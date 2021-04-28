#include "extensions/filters/http/ip_tagging/ip_tagging_filter.h"

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.h"
#include "envoy/protobuf/message_validator.h"

#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

IpTaggingFilterStats::IpTaggingFilterStats(Stats::Scope& scope, const std::string& stat_prefix)
    : scope_(scope), stat_name_set_(scope.symbolTable().makeSet("IpTagging")),
      stats_prefix_(stat_name_set_->add(stat_prefix + "ip_tagging")),
      no_hit_(stat_name_set_->add("no_hit")), total_(stat_name_set_->add("total")),
      unknown_tag_(stat_name_set_->add("unknown_tag.hit")) {}

void IpTaggingFilterStats::incCounter(Stats::StatName name) {
  Stats::SymbolTable::StoragePtr storage = scope_.symbolTable().join({stats_prefix_, name});
  scope_.counterFromStatName(Stats::StatName(storage.get())).inc();
}

IpTaggingFilterStats::~IpTaggingFilterStats() = default;

LcTrieWithStats::LcTrieWithStats(
    std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>& trie,
    Stats::Scope& scope, const std::string& stats_prefix)
    : trie_(trie), filter_stats_(scope, stats_prefix) {

  std::vector<std::string> all_tags;
  std::transform(trie.begin(), trie.end(), std::back_inserter(all_tags),
                 [](const auto& pair) { return pair.first; });

  for (const std::string& tag : all_tags) {
    filter_stats_.setTags(tag);
  }
}

std::vector<std::string>
LcTrieWithStats::resolveTagsForIpAddress(Network::Address::InstanceConstSharedPtr& Ipaddress) {

  std::vector<std::string> tags = trie_.getData(Ipaddress);

  if (!tags.empty()) {
    // For a large number(ex > 1000) of tags, stats cardinality will be an issue.
    // If there are use cases with a large set of tags, a way to opt into these stats
    // should be exposed and other observability options like logging tags need to be implemented.
    for (const std::string& tag : tags) {
      filter_stats_.incHit(tag);
    }
  } else {
    filter_stats_.incNoHit();
  }
  filter_stats_.incTotal();
  return tags;
}

LcTrieWithStats::~LcTrieWithStats() = default;

IpTaggingFilterConfig::IpTaggingFilterConfig(
    const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& config,
    const std::string& stat_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
    Envoy::Server::Configuration::FactoryContext& factory_context)
    : request_type_(requestTypeEnum(config.request_type())), runtime_(runtime) {

  // Once loading IP tags from a file system is supported, the restriction on the size
  // of the set should be removed and observability into what tags are loaded needs
  // to be implemented.
  // TODO(ccaraman): Remove size check once file system support is implemented.
  // Work is tracked by issue https://github.com/envoyproxy/envoy/issues/2695.
  if (!config.ip_tags().empty() && !config.path().empty()) {
    throw EnvoyException("Only one of path or ip_tags can be specified");
  }

  if (!config.path().empty()) {
    watcher_ = TagSetWatcher::create(factory_context, config.path(), scope, stat_prefix);

  } else if (!config.ip_tags().empty()) {
    const IPTagsProto tags = config.ip_tags();
    std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>> tag_data =
        IpTaggingFilterSetTagData(tags);
    triestat_ = std::make_shared<LcTrieWithStats>(tag_data, scope, stat_prefix);

  } else {
    throw EnvoyException(
        "HTTP IP Tagging Filter requires one of ip_tags and path to be specified.");
  }
}

std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>
IpTaggingFilterConfig::IpTaggingFilterSetTagData(const IPTagsProto& ip_tags) {

  std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>> tag_data;
  tag_data.reserve(ip_tags.size());

  for (const auto& ip_tag : ip_tags) {
    std::vector<Network::Address::CidrRange> cidr_set;
    cidr_set.reserve(ip_tag.ip_list().size());
    for (const envoy::config::core::v3::CidrRange& entry : ip_tag.ip_list()) {

      // Currently, CidrRange::create doesn't guarantee that the CidrRanges are valid.
      Network::Address::CidrRange cidr_entry = Network::Address::CidrRange::create(entry);
      if (cidr_entry.isValid()) {
        cidr_set.emplace_back(std::move(cidr_entry));
      } else {
        throw EnvoyException(
            fmt::format("invalid ip/mask combo '{}/{}' (format is <ip>/<# mask bits>)",
                        entry.address_prefix(), entry.prefix_len().value()));
      }
    }

    tag_data.emplace_back(ip_tag.ip_tag_name(), cidr_set);
  }
  return tag_data;
}

std::shared_ptr<TagSetWatcher>
TagSetWatcher::create(Server::Configuration::FactoryContext& factory_context, std::string filename,
                      Stats::Scope& scope, const std::string& stat_prefix) {

  auto ptr =  std::make_shared<TagSetWatcher>(factory_context, std::move(filename), scope, stat_prefix);
  return ptr;
}

TagSetWatcher::TagSetWatcher(Server::Configuration::FactoryContext& factory_context,
                             Event::Dispatcher& dispatcher, Api::Api& api, std::string filename,
                             Stats::Scope& scope, const std::string& stat_prefix)
    : api_(api), filename_(filename), watcher_(dispatcher.createFilesystemWatcher()),
      factory_context_(factory_context), scope_(scope), stat_prefix_(stat_prefix),
      yaml(absl::EndsWith(filename, MessageUtil::FileExtensions::get().Yaml)) {

  const auto split_path = api_.fileSystem().splitPathFromFilename(filename);
  watcher_->addWatch(absl::StrCat(split_path.directory_, "/"), Filesystem::Watcher::Events::MovedTo,
                     [this]([[maybe_unused]] std::uint32_t event) { maybeUpdate_(); });

  maybeUpdate_(true);
}

// read the file from filesystem, load the content and only update if the hash has changed.
void TagSetWatcher::maybeUpdate_(bool force) {
  std::string contents = api_.fileSystem().fileReadToEnd(filename_);

  uint64_t hash = 0;
  hash = HashUtil::xxHash64(contents, hash);

  if (force || hash != content_hash_)
    update_(std::move(contents), hash);
}

void TagSetWatcher::update_(absl::string_view contents, std::uint64_t hash) {
  std::shared_ptr<LcTrieWithStats> new_values = fileContentsAsTagSet_(contents);

  new_values.swap(triestat_); // Fastest way to replace the values.
  content_hash_ = hash;
}

// Validate and parse both yaml and json file content to proto.
IpTagFileProto TagSetWatcher::protoFromFileContents_(absl::string_view contents) const {
  const std::string file_content = std::string(contents);
  IpTagFileProto ipf;

  if (yaml) {
    MessageUtil::loadFromYaml(file_content, ipf, protoValidator());
  } else {
    MessageUtil::loadFromJson(file_content, ipf, protoValidator());
  }
  return ipf;
}

// take proto content and convert it into LcTrie with stats
std::shared_ptr<LcTrieWithStats>
TagSetWatcher::fileContentsAsTagSet_(absl::string_view contents) const {

  IpTagFileProto proto_content = protoFromFileContents_(contents);

  std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>> tag_data =
      IpTaggingFilterConfig::IpTaggingFilterSetTagData(proto_content.ip_tags());

  return std::make_shared<LcTrieWithStats>(tag_data, scope_, stat_prefix_);
}

TagSetWatcher::~TagSetWatcher() = default;

IpTaggingFilter::IpTaggingFilter(IpTaggingFilterConfigSharedPtr config) : config_(config) {}

IpTaggingFilter::~IpTaggingFilter() = default;

void IpTaggingFilter::onDestroy() {}

Http::FilterHeadersStatus IpTaggingFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  const bool is_internal_request = headers.EnvoyInternalRequest() &&
                                   (headers.EnvoyInternalRequest()->value() ==
                                    Http::Headers::get().EnvoyInternalRequestValues.True.c_str());

  if ((is_internal_request && config_->requestType() == FilterRequestType::EXTERNAL) ||
      (!is_internal_request && config_->requestType() == FilterRequestType::INTERNAL) ||
      !config_->runtime().snapshot().featureEnabled("ip_tagging.http_filter_enabled", 100)) {
    return Http::FilterHeadersStatus::Continue;
  }

  // TODO
  // We must clear the route cache or else we can't match on x-envoy-ip-tags.
  callbacks_->clearRouteCache();

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus IpTaggingFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus IpTaggingFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void IpTaggingFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
