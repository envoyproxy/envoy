#include "source/extensions/filters/http/cache/file_system_http_cache/cache_entry.h"

#include "source/extensions/filters/http/cache/file_system_http_cache/cache_file_fixed_block.h"
#include "source/extensions/filters/http/cache/file_system_http_cache/cache_file_header_proto_util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

std::shared_ptr<CacheEntryWorkInProgress>
CacheEntryWorkInProgress::create(std::shared_ptr<CacheEntryFile> entry_being_replaced) {
  return std::shared_ptr<CacheEntryWorkInProgress>(
      new CacheEntryWorkInProgress(std::move(entry_being_replaced)));
}

std::shared_ptr<CacheEntryFile> CacheEntryFile::create(AsyncFileManager& file_manager,
                                                       std::string&& filename) {
  return std::shared_ptr<CacheEntryFile>(new CacheEntryFile(file_manager, std::move(filename)));
}

CacheEntryFile::CacheEntryFile(AsyncFileManager& file_manager, std::string&& filename)
    : file_manager_(file_manager), filename_(filename) {}

CacheEntryFile::~CacheEntryFile() { on_destroy_(); }

void CacheEntryFile::setHeaderData(const CacheFileFixedBlock& block, CacheFileHeader proto) {
  absl::MutexLock lock(&header_mu_);
  header_data_.block = block;
  header_data_.proto = proto;
}

CacheEntryFile::HeaderData CacheEntryFile::getHeaderData() {
  absl::MutexLock lock(&header_mu_);
  return header_data_;
}

std::shared_ptr<CacheEntryVaryRedirect>
CacheEntryVaryRedirect::create(const absl::btree_set<absl::string_view>& vary_header_values) {
  return std::shared_ptr<CacheEntryVaryRedirect>(new CacheEntryVaryRedirect(vary_header_values));
}

CacheEntryVaryRedirect::CacheEntryVaryRedirect(
    const absl::btree_set<absl::string_view>& vary_header_values)
    : vary_header_values_copy_(absl::StrJoin(vary_header_values, ",")),
      vary_header_values_(absl::StrSplit(vary_header_values_copy_, ',')) {}

void CacheEntry::touch(TimeSource& time_source) { last_touched_ = time_source.systemTime(); }

absl::optional<Key>
CacheEntryVaryRedirect::varyKey(const Key& base, const VaryAllowList& vary_allow_list,
                                const Http::RequestHeaderMap& request_headers) const {
  const absl::optional<std::string> vary_identifier =
      VaryHeaderUtils::createVaryIdentifier(vary_allow_list, vary_header_values_, request_headers);
  if (!vary_identifier.has_value()) {
    // Skip the insert if we are unable to create a vary key.
    return absl::nullopt;
  }
  Key vary_key = base;
  vary_key.add_custom_fields(vary_identifier.value());
  return vary_key;
}

Buffer::OwnedImpl CacheEntryVaryRedirect::asBuffer() const {
  Buffer::OwnedImpl buffer;
  CacheFileFixedBlock block;
  CacheFileHeader header;
  auto h = header.add_headers();
  h->set_key(Http::CustomHeaders::get().Vary.get());
  h->set_value(vary_header_values_copy_);
  std::string serialized_headers = serializedStringFromProto(header);
  block.setHeadersSize(serialized_headers.length());
  buffer.add(block.stringView());
  buffer.add(serialized_headers);
  return buffer;
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
