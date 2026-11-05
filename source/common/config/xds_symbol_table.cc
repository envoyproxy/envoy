#include "source/common/config/xds_symbol_table.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Envoy {
namespace Config {

XdsSymbolTableSharedPtr XdsSymbolTable::create() {
  return std::shared_ptr<XdsSymbolTable>(new XdsSymbolTable());
}

XdsSymbolTable::XdsSymbolTable() {
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

XdsSymbol XdsSymbolTable::toSymbol(absl::string_view token) {
  if (token.empty()) {
    return 0;
  }

  auto it = encode_map_.find(token);
  if (it != encode_map_.end()) {
    return it->second;
  }

  XdsSymbol sym;
  if (!free_list_.empty()) {
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

std::vector<XdsSymbolTable::TokenItem> XdsSymbolTable::splitTokens(absl::string_view name) {
  std::vector<TokenItem> items;
  items.reserve(name.size() / 4 + 4);

  size_t i = 0;
  size_t token_start = 0;

  auto flush_token = [&](size_t end) {
    if (end > token_start) {
      items.push_back({0, name.substr(token_start, end - token_start)});
    }
  };

  while (i < name.size()) {
    // Check for "://"
    if (i + 2 < name.size() && name[i] == ':' && name[i + 1] == '/' && name[i + 2] == '/') {
      flush_token(i);
      items.push_back({SchemeSep, {}});
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
      flush_token(i);
      items.push_back({delimiter_sym, {}});
      ++i;
      token_start = i;
    } else {
      ++i;
    }
  }

  flush_token(name.size());
  return items;
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
  forEachSymbol(bytes, [this](XdsSymbol s) ABSL_NO_THREAD_SAFETY_ANALYSIS {
    if (s >= FirstDynamicSymbol && s < ref_counts_.size()) {
      if (ref_counts_[s] < std::numeric_limits<uint32_t>::max()) {
        if (--ref_counts_[s] == 0) {
          encode_map_.erase(decode_table_[s]);
          decode_table_[s].clear();
          free_list_.push_back(s);
        }
      }
    }
  });
}

void XdsSymbolTable::releaseRef(absl::Span<const XdsSymbol> symbols) {
  Thread::LockGuard lock(lock_);
  for (XdsSymbol s : symbols) {
    if (s >= FirstDynamicSymbol && s < ref_counts_.size()) {
      if (ref_counts_[s] < std::numeric_limits<uint32_t>::max()) {
        if (--ref_counts_[s] == 0) {
          encode_map_.erase(decode_table_[s]);
          decode_table_[s].clear();
          free_list_.push_back(s);
        }
      }
    }
  }
}

XdsResourceNameStorage XdsSymbolTable::encode(absl::string_view name) {
  std::vector<TokenItem> tokens = splitTokens(name);
  std::vector<XdsSymbol> symbols;
  symbols.reserve(tokens.size());

  {
    Thread::LockGuard lock(lock_);
    for (const auto& item : tokens) {
      if (item.delimiter_sym != 0) {
        symbols.push_back(item.delimiter_sym);
      } else {
        symbols.push_back(toSymbol(item.word));
      }
    }
    addRefLocked(symbols);
  }

  std::vector<uint8_t> bytes = encodeSymbolsToBytes(symbols);
  std::unique_ptr<uint8_t[]> data;
  if (!bytes.empty()) {
    data = std::make_unique<uint8_t[]>(bytes.size());
    std::memcpy(data.get(), bytes.data(), bytes.size()); // NOLINT(safe-memcpy)
  }
  return {shared_from_this(), std::move(data), static_cast<uint32_t>(bytes.size())};
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
