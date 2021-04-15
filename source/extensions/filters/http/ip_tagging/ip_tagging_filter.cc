#include "extensions/filters/http/ip_tagging/ip_tagging_filter.h"

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.h"
#include "envoy/protobuf/message_validator.h"

#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"

#include "absl/strings/str_join.h"
#include "yaml-cpp/yaml.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

IpTaggingFilterConfig::IpTaggingFilterConfig(
    const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& config,
    const std::string& stat_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
    Envoy::Server::Configuration::FactoryContext& factory_context)
    : request_type_(requestTypeEnum(config.request_type())), scope_(scope), runtime_(runtime),
      stat_name_set_(scope.symbolTable().makeSet("IpTagging")),
      stats_prefix_(stat_name_set_->add(stat_prefix + "ip_tagging")),
      no_hit_(stat_name_set_->add("no_hit")), total_(stat_name_set_->add("total")),
      unknown_tag_(stat_name_set_->add("unknown_tag.hit")) {

  // Once loading IP tags from a file system is supported, the restriction on the size
  // of the set should be removed and observability into what tags are loaded needs
  // to be implemented.
  // TODO(ccaraman): Remove size check once file system support is implemented.
  // Work is tracked by issue https://github.com/envoyproxy/envoy/issues/2695.
  if (!config.ip_tags().empty() && !config.path().empty()) {
    throw EnvoyException("Only one of path or ip_tags can be specified");
  }

  if (!config.path().empty()) {

    watcher_ = ValueSetWatcher::create(factory_context, config.path());

  } else if (!config.ip_tags().empty()) {

    std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>> tag_data =
        IpTaggingFilterSetTagData(config);
    trie_ = std::make_unique<Network::LcTrie::LcTrie<std::string>>(tag_data);

  } else {

    throw EnvoyException(
        "HTTP IP Tagging Filter requires one of ip_tags and path to be specified.");
  }
}

std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>
IpTaggingFilterConfig::IpTaggingFilterSetTagData(
    const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& config) {

  std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>> tag_data;
  tag_data.reserve(config.ip_tags().size());

  for (const auto& ip_tag : config.ip_tags()) {
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
    stat_name_set_->rememberBuiltin(absl::StrCat(ip_tag.ip_tag_name(), ".hit"));
  }
  return tag_data;
}

ValueSet::~ValueSet() = default;

// Check the registry and either return a watcher for a file or create one
std::shared_ptr<ValueSetWatcher>
ValueSetWatcher::create(Server::Configuration::FactoryContext& factory_context,
                        std::string filename) {
  return Registry::singleton().getOrCreate(factory_context, std::move(filename));
}

ValueSetWatcher::ValueSetWatcher(Server::Configuration::FactoryContext& factory_context,
                                 Event::Dispatcher& dispatcher, Api::Api& api, std::string filename)
    : api_(api), filename_(filename), watcher_(dispatcher.createFilesystemWatcher()),
      factory_context_(factory_context) {
  const auto split_path = api_.fileSystem().splitPathFromFilename(filename);
  watcher_->addWatch(absl::StrCat(split_path.directory_, "/"), Filesystem::Watcher::Events::MovedTo,
                     [this]([[maybe_unused]] std::uint32_t event) { maybeUpdate_(); });

  maybeUpdate_(true);
}

ValueSetWatcher::~ValueSetWatcher() {
  if (registry_ != nullptr)
    registry_->remove(*this);
}

std::shared_ptr<const ValueSet> ValueSetWatcher::get() const { return values_; }

// read the file from filesystem, load the content and only update if the hash has changed.
void ValueSetWatcher::maybeUpdate_(bool force) {
  std::string contents = api_.fileSystem().fileReadToEnd(filename_);

  uint64_t hash = 0;
  hash = HashUtil::xxHash64(contents, hash);

  if (force || hash != content_hash_)
    update_(std::move(contents), hash);
}

void ValueSetWatcher::update_(absl::string_view contents, std::uint64_t hash) {
  std::shared_ptr<const ValueSet> new_values = fileContentsAsValueSet_(contents);

  new_values.swap(values_); // Fastest way to replace the values.
  content_hash_ = hash;
}

// Validate and parse both yaml and json file content.
std::shared_ptr<ValueSet>
ValueSetWatcher::fileContentsAsValueSet_(absl::string_view contents) const {
  const std::string file_content = std::string(contents);
  std::shared_ptr<ValueSet> values = std::make_shared<ValueSet>();
  IpTagFileProto ipf;

  if (extension_ == "Yaml") {
    MessageUtil::loadFromYaml(file_content, ipf, protoValidator());
    values = yamlFileContentsAsValueSet_(std::move(file_content));

  } else if (extension_ == "Json") {
    MessageUtil::loadFromJson(file_content, ipf, protoValidator());
    values = jsonFileContentsAsValueSet_(std::move(file_content));

  } else {
    throw EnvoyException("HTTP IP Tagging Filter supports only json or yaml file types");
  }

  return values;
}

// Json parser
std::shared_ptr<ValueSet>
ValueSetWatcher::jsonFileContentsAsValueSet_(std::string contents) const {
  std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>> tag_data;
  Json::ObjectSharedPtr json_data = Json::Factory::loadFromString(contents);
  int pos = 0;

  json_data->iterate([&pos](const std::string& key, const Json::Object& value) {
    std::string ip_tag_name = value.getString("ip_tag_name");
    std::vector<Envoy::Json::ObjectSharedPtr> tag_list = value.getObjectArray("ip_tags");
    tag_data.emplace_back(ip_tag_name, tag_list);
    pos++;
  });

  return std::make_unique<Network::LcTrie::LcTrie<std::string>>(tag_data);
}

// Yaml parser
// TODO: techincally yaml is a superset of json so jaml parser should work for json content as well
// in an ideal world we should be able to just use one parser based on that
std::shared_ptr<ValueSet>
ValueSetWatcher::yamlFileContentsAsValueSet_(std::string contents) const {
  std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>> tag_data;
  YAML::Node data = YAML::Load(contents);

  for (std::size_t i=0;i<data.size();i++) {
    std::string ip_tag_name = data[i]["ip_tag_name"].as<std::string>();
    std::vector<Network::Address::CidrRange> tag_list =
        data[i]["ip_tags"].as<std::vector<Network::Address::CidrRange>>();
    tag_data.emplace_back(ip_tag_name, tag_list);
  }
  return std::make_unique<Network::LcTrie::LcTrie<std::string>>(tag_data);
}

std::shared_ptr<ValueSetWatcher>
ValueSetWatcher::Registry::getOrCreate(Server::Configuration::FactoryContext& factory_context,
                                       std::string filename) {
  Thread::LockGuard lock(mtx_);

  auto& ptr_ref = map_[filename];
  auto ptr = ptr_ref.lock();
  if (ptr != nullptr)
    return ptr;

  ptr_ref = ptr = std::make_shared<ValueSetWatcher>(factory_context, std::move(filename));
  ptr->registry_ = this;
  return ptr;
}

void ValueSetWatcher::Registry::remove(ValueSetWatcher& watcher) noexcept {
  Thread::LockGuard lock(mtx_);

  // This is safe, even if the registered watcher is not the same
  // (which is not something that can happen).
  // The registry only promotes sharing, but if the wrong watcher is
  // erased, it simply means it won't be shared anymore.
  map_.erase(watcher.filename());
}

ValueSetWatcher::Registry& ValueSetWatcher::Registry::singleton() {
  static Registry impl;
  return impl;
}

void IpTaggingFilterConfig::incCounter(Stats::StatName name) {
  Stats::SymbolTable::StoragePtr storage = scope_.symbolTable().join({stats_prefix_, name});
  scope_.counterFromStatName(Stats::StatName(storage.get())).inc();
}

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

  std::vector<std::string> tags;

  if (watcher_ != nullptr) {
    tags = watcher_->get();
  } else {
    tags = config_->trie().getData(callbacks_->streamInfo().downstreamRemoteAddress());
  }

  if (!tags.empty()) {
    const std::string tags_join = absl::StrJoin(tags, ",");
    headers.appendEnvoyIpTags(tags_join, ",");

    // We must clear the route cache or else we can't match on x-envoy-ip-tags.
    callbacks_->clearRouteCache();

    // For a large number(ex > 1000) of tags, stats cardinality will be an issue.
    // If there are use cases with a large set of tags, a way to opt into these stats
    // should be exposed and other observability options like logging tags need to be implemented.
    for (const std::string& tag : tags) {
      config_->incHit(tag);
    }
  } else {
    config_->incNoHit();
  }
  config_->incTotal();
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
