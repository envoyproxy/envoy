#include "source/common/config/xds_symbol_table.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "absl/base/optimization.h"

namespace Envoy {
namespace Config {
namespace {

std::atomic<uint64_t> next_symbol_table_id{1};

struct ThreadLocalEncodeCache {
  uint64_t table_id{0};
  uint64_t synced_version{0};
  uint64_t last_seen_version{0};
  size_t decode_table_size{0};
  absl::flat_hash_map<std::string, XdsSymbol> encode_map;
};

} // namespace

XdsSymbolTableSharedPtr XdsSymbolTable::create() {
  return std::shared_ptr<XdsSymbolTable>(new XdsSymbolTable());
}

XdsSymbolTable::XdsSymbolTable()
    : table_id_(next_symbol_table_id.fetch_add(1, std::memory_order_relaxed)) {
  Thread::LockGuard lock(lock_);
  // Pre-seed reserved symbols 0..9.
  decode_table_.push_back("");    // 0: unused
  decode_table_.push_back("/");   // 1: Slash
  decode_table_.push_back(".");   // 2: Dot
  decode_table_.push_back(":");   // 3: Colon
  decode_table_.push_back("?");   // 4: Question
  decode_table_.push_back("&");   // 5: Ampersand
  decode_table_.push_back("=");   // 6: Equals
  decode_table_.push_back("_");   // 7: Underscore
  decode_table_.push_back("-");   // 8: Dash
  decode_table_.push_back("://"); // 9: SchemeSep

  ref_counts_.assign(10, 1); // Reserved symbols have immortal ref count.

  for (size_t i = 1; i < decode_table_.size(); ++i) {
    encode_map_[decode_table_[i]] = static_cast<XdsSymbol>(i);
  }
}

void XdsSymbolTable::recordMutationLocked(bool erased_or_reused_slot) {
  const uint64_t next_version = version_.load(std::memory_order_relaxed) + 1;
  if (erased_or_reused_slot) {
    last_erase_version_ = next_version;
  }
  version_.store(next_version, std::memory_order_release);
}

XdsSymbol XdsSymbolTable::toSymbol(absl::string_view token, bool& added_symbol, bool& reused_slot) {
  if (token.empty()) {
    return 0;
  }

  auto it = encode_map_.find(token);
  if (it != encode_map_.end()) {
    return it->second;
  }

  added_symbol = true;
  XdsSymbol sym;
  if (!free_list_.empty()) {
    reused_slot = true;
    sym = free_list_.back();
    free_list_.pop_back();
    decode_table_[sym] = std::string(token);
    ref_counts_[sym] = 0;
  } else {
    sym = static_cast<XdsSymbol>(decode_table_.size());
    decode_table_.emplace_back(token);
    ref_counts_.push_back(0);
  }

  encode_map_[decode_table_[sym]] = sym;
  return sym;
}

template <typename Callback>
bool XdsSymbolTable::forEachToken(absl::string_view name, Callback&& callback) {
  size_t i = 0;
  size_t token_start = 0;

  auto flush_token = [&](size_t end) -> bool {
    if (end > token_start) {
      return callback(XdsSymbol{0}, name.substr(token_start, end - token_start));
    }
    return true;
  };

  while (i < name.size()) {
    // Check for "://"
    if (i + 2 < name.size() && name[i] == ':' && name[i + 1] == '/' && name[i + 2] == '/') {
      if (!flush_token(i)) {
        return false;
      }
      callback(SchemeSep, {});
      i += 3;
      token_start = i;
      continue;
    }

    char c = name[i];
    XdsSymbol delimiter_sym = 0;
    switch (c) {
    case '/':
      delimiter_sym = Slash;
      break;
    case '.':
      delimiter_sym = Dot;
      break;
    case ':':
      delimiter_sym = Colon;
      break;
    case '?':
      delimiter_sym = Question;
      break;
    case '&':
      delimiter_sym = Ampersand;
      break;
    case '=':
      delimiter_sym = Equals;
      break;
    case '_':
      delimiter_sym = Underscore;
      break;
    case '-':
      delimiter_sym = Dash;
      break;
    default:
      delimiter_sym = 0;
      break;
    }

    if (delimiter_sym != 0) {
      if (!flush_token(i)) {
        return false;
      }
      callback(delimiter_sym, {});
      ++i;
      token_start = i;
    } else {
      ++i;
    }
  }

  return flush_token(name.size());
}

absl::InlinedVector<XdsSymbolTable::TokenItem, 64>
XdsSymbolTable::splitTokens(absl::string_view name) {
  absl::InlinedVector<TokenItem, 64> items;
  forEachToken(name, [&items](XdsSymbol delimiter_sym, absl::string_view word) {
    items.push_back({delimiter_sym, word});
    return true;
  });
  return items;
}

template <typename Map>
bool XdsSymbolTable::tryEncodeWithMap(absl::string_view name, const Map& encode_map,
                                      absl::InlinedVector<uint8_t, 64>& scratch) {
  return forEachToken(name,
                      [&encode_map, &scratch](XdsSymbol delimiter_sym, absl::string_view word) {
                        XdsSymbol sym = delimiter_sym;
                        if (sym == 0) {
                          const auto it = encode_map.find(word);
                          if (it == encode_map.end()) {
                            return false;
                          }
                          sym = it->second;
                        }
                        while (sym >= 128) {
                          scratch.push_back(static_cast<uint8_t>((sym & 0x7F) | 0x80));
                          sym >>= 7;
                        }
                        scratch.push_back(static_cast<uint8_t>(sym & 0x7F));
                        return true;
                      });
}

std::vector<uint8_t> XdsSymbolTable::encodeSymbolsToBytes(absl::Span<const XdsSymbol> symbols) {
  std::vector<uint8_t> bytes;
  bytes.reserve(symbols.size() * 2);
  for (XdsSymbol sym : symbols) {
    while (sym >= 128) {
      bytes.push_back(static_cast<uint8_t>((sym & 0x7F) | 0x80));
      sym >>= 7;
    }
    bytes.push_back(static_cast<uint8_t>(sym & 0x7F));
  }
  return bytes;
}

std::vector<XdsSymbol> XdsSymbolTable::decodeBytesToSymbols(absl::Span<const uint8_t> bytes) {
  std::vector<XdsSymbol> symbols;
  bool ok = forEachSymbol(bytes, [&symbols](XdsSymbol s) { symbols.push_back(s); });
  if (!ok) {
    return {};
  }
  return symbols;
}

void XdsSymbolTable::addRefLocked(absl::Span<const XdsSymbol> symbols) {
  for (XdsSymbol s : symbols) {
    if (s >= FirstDynamicSymbol && s < ref_counts_.size()) {
      if (ref_counts_[s] < std::numeric_limits<uint32_t>::max()) {
        ++ref_counts_[s];
      }
    }
  }
}

void XdsSymbolTable::addRef(absl::Span<const uint8_t> bytes) {
  Thread::LockGuard lock(lock_);
  forEachSymbol(bytes, [this](XdsSymbol s) ABSL_NO_THREAD_SAFETY_ANALYSIS {
    if (s >= FirstDynamicSymbol && s < ref_counts_.size()) {
      if (ref_counts_[s] < std::numeric_limits<uint32_t>::max()) {
        ++ref_counts_[s];
      }
    }
  });
}

void XdsSymbolTable::addRef(absl::Span<const XdsSymbol> symbols) {
  Thread::LockGuard lock(lock_);
  addRefLocked(symbols);
}

void XdsSymbolTable::releaseRef(absl::Span<const uint8_t> bytes) {
  Thread::LockGuard lock(lock_);
  bool erased_symbol = false;
  forEachSymbol(bytes, [this, &erased_symbol](XdsSymbol s) ABSL_NO_THREAD_SAFETY_ANALYSIS {
    if (s >= FirstDynamicSymbol && s < ref_counts_.size()) {
      if (ref_counts_[s] < std::numeric_limits<uint32_t>::max()) {
        if (--ref_counts_[s] == 0) {
          encode_map_.erase(decode_table_[s]);
          decode_table_[s].clear();
          free_list_.push_back(s);
          erased_symbol = true;
        }
      }
    }
  });
  if (erased_symbol) {
    recordMutationLocked(/*erased_or_reused_slot=*/true);
  }
}

void XdsSymbolTable::releaseRef(absl::Span<const XdsSymbol> symbols) {
  Thread::LockGuard lock(lock_);
  bool erased_symbol = false;
  for (XdsSymbol s : symbols) {
    if (s >= FirstDynamicSymbol && s < ref_counts_.size()) {
      if (ref_counts_[s] < std::numeric_limits<uint32_t>::max()) {
        if (--ref_counts_[s] == 0) {
          encode_map_.erase(decode_table_[s]);
          decode_table_[s].clear();
          free_list_.push_back(s);
          erased_symbol = true;
        }
      }
    }
  }
  if (erased_symbol) {
    recordMutationLocked(/*erased_or_reused_slot=*/true);
  }
}

XdsResourceNameStorage XdsSymbolTable::encode(absl::string_view name) {
  const absl::InlinedVector<TokenItem, 64> tokens = splitTokens(name);
  std::vector<XdsSymbol> symbols;
  symbols.reserve(tokens.size());

  {
    Thread::LockGuard lock(lock_);
    bool added_symbol = false;
    bool reused_slot = false;
    for (const auto& item : tokens) {
      if (item.delimiter_sym != 0) {
        symbols.push_back(item.delimiter_sym);
      } else {
        symbols.push_back(toSymbol(item.word, added_symbol, reused_slot));
      }
    }
    addRefLocked(symbols);
    if (added_symbol) {
      recordMutationLocked(reused_slot);
    }
  }

  std::vector<uint8_t> bytes = encodeSymbolsToBytes(symbols);
  std::unique_ptr<uint8_t[]> data;
  if (!bytes.empty()) {
    data = std::make_unique<uint8_t[]>(bytes.size());
    std::memcpy(data.get(), bytes.data(), bytes.size()); // NOLINT(safe-memcpy)
  }
  return {shared_from_this(), std::move(data), static_cast<uint32_t>(bytes.size())};
}

std::optional<XdsResourceName>
XdsSymbolTable::tryEncode(absl::string_view name, absl::InlinedVector<uint8_t, 64>& scratch) const {
  scratch.clear();
  if (name.empty()) {
    return XdsResourceName{};
  }

  static thread_local ThreadLocalEncodeCache tls_cache;
  const uint64_t current_version = version_.load(std::memory_order_acquire);
  if (ABSL_PREDICT_TRUE(tls_cache.table_id == table_id_ &&
                        tls_cache.synced_version == current_version)) {
    if (!tryEncodeWithMap(name, tls_cache.encode_map, scratch)) {
      scratch.clear();
      return std::nullopt;
    }
    return XdsResourceName{scratch.data(), scratch.size()};
  }

  Thread::LockGuard lock(lock_);
  const uint64_t locked_version = version_.load(std::memory_order_relaxed);
  if (tls_cache.table_id != table_id_ || tls_cache.last_seen_version != locked_version) {
    if (tls_cache.table_id != table_id_) {
      tls_cache.table_id = table_id_;
      tls_cache.synced_version = 0;
      tls_cache.decode_table_size = 0;
    }
    tls_cache.last_seen_version = locked_version;
    if (!tryEncodeWithMap(name, encode_map_, scratch)) {
      scratch.clear();
      return std::nullopt;
    }
    return XdsResourceName{scratch.data(), scratch.size()};
  }

  // The symbol table version has stabilized across consecutive lookups on this thread;
  // synchronize the thread-local map so subsequent lookups run lock-free.
  if (tls_cache.synced_version == 0 || tls_cache.synced_version < last_erase_version_) {
    tls_cache.encode_map.clear();
    tls_cache.encode_map.reserve(encode_map_.size());
    for (const auto& [token, sym] : encode_map_) {
      tls_cache.encode_map.emplace(token, sym);
    }
  } else {
    for (size_t s = tls_cache.decode_table_size; s < decode_table_.size(); ++s) {
      tls_cache.encode_map.emplace(decode_table_[s], static_cast<XdsSymbol>(s));
    }
  }
  tls_cache.decode_table_size = decode_table_.size();
  tls_cache.synced_version = locked_version;

  if (!tryEncodeWithMap(name, tls_cache.encode_map, scratch)) {
    scratch.clear();
    return std::nullopt;
  }
  return XdsResourceName{scratch.data(), scratch.size()};
}

std::string XdsSymbolTable::toString(const XdsResourceName& name) const {
  if (name.empty()) {
    return "";
  }
  Thread::LockGuard lock(lock_);
  size_t total_len = 0;
  forEachSymbol(name.span(), [this, &total_len](XdsSymbol s) ABSL_NO_THREAD_SAFETY_ANALYSIS {
    if (s < decode_table_.size()) {
      total_len += decode_table_[s].size();
    }
  });

  std::string result;
  result.reserve(total_len);
  forEachSymbol(name.span(), [this, &result](XdsSymbol s) ABSL_NO_THREAD_SAFETY_ANALYSIS {
    if (s < decode_table_.size()) {
      result.append(decode_table_[s]);
    }
  });
  return result;
}

std::string XdsSymbolTable::toString(absl::Span<const XdsSymbol> symbols) const {
  Thread::LockGuard lock(lock_);
  size_t total_len = 0;
  for (XdsSymbol s : symbols) {
    if (s < decode_table_.size()) {
      total_len += decode_table_[s].size();
    }
  }
  std::string result;
  result.reserve(total_len);
  for (XdsSymbol s : symbols) {
    if (s < decode_table_.size()) {
      result.append(decode_table_[s]);
    }
  }
  return result;
}

size_t XdsSymbolTable::numSymbols() const {
  Thread::LockGuard lock(lock_);
  return decode_table_.size();
}

size_t XdsSymbolTable::numActiveSymbols() const {
  Thread::LockGuard lock(lock_);
  return encode_map_.size();
}

// ============================================================================
// XdsResourceNameStorage implementation
// ============================================================================

XdsResourceNameStorage::XdsResourceNameStorage(const XdsSymbolTableSharedPtr& table,
                                               absl::string_view name) {
  if (table != nullptr) {
    *this = table->encode(name);
  }
}

XdsResourceNameStorage::XdsResourceNameStorage(XdsSymbolTableSharedPtr table,
                                               std::unique_ptr<uint8_t[]> data, uint32_t size)
    : table_(std::move(table)), data_(std::move(data)), size_(size) {}

XdsResourceNameStorage::~XdsResourceNameStorage() {
  if (table_ != nullptr && data_ != nullptr && size_ > 0) {
    table_->releaseRef({data_.get(), size_});
  }
}

XdsResourceNameStorage::XdsResourceNameStorage(XdsResourceNameStorage&& other) noexcept
    : table_(std::move(other.table_)), data_(std::move(other.data_)), size_(other.size_) {
  other.size_ = 0;
}

XdsResourceNameStorage& XdsResourceNameStorage::operator=(XdsResourceNameStorage&& other) noexcept {
  if (this != &other) {
    if (table_ != nullptr && data_ != nullptr && size_ > 0) {
      table_->releaseRef({data_.get(), size_});
    }
    table_ = std::move(other.table_);
    data_ = std::move(other.data_);
    size_ = other.size_;
    other.size_ = 0;
  }
  return *this;
}

XdsResourceNameStorage::XdsResourceNameStorage(const XdsResourceNameStorage& other)
    : table_(other.table_), size_(other.size_) {
  if (other.data_ != nullptr && other.size_ > 0) {
    data_ = std::make_unique<uint8_t[]>(size_);
    std::memcpy(data_.get(), other.data_.get(), size_); // NOLINT(safe-memcpy)
    if (table_ != nullptr) {
      table_->addRef({data_.get(), size_});
    }
  }
}

XdsResourceNameStorage& XdsResourceNameStorage::operator=(const XdsResourceNameStorage& other) {
  if (this != &other) {
    if (table_ != nullptr && data_ != nullptr && size_ > 0) {
      table_->releaseRef({data_.get(), size_});
    }
    table_ = other.table_;
    size_ = other.size_;
    if (other.data_ != nullptr && other.size_ > 0) {
      data_ = std::make_unique<uint8_t[]>(size_);
      std::memcpy(data_.get(), other.data_.get(), size_); // NOLINT(safe-memcpy)
      if (table_ != nullptr) {
        table_->addRef({data_.get(), size_});
      }
    } else {
      data_.reset();
    }
  }
  return *this;
}

} // namespace Config
} // namespace Envoy
