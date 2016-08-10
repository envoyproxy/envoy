#include "header_map_impl.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/utility.h"

namespace Http {

HeaderMapImpl::HeaderMapImpl(const HeaderMap& rhs) {
  rhs.iterate([&](const LowerCaseString& key, const std::string& value)
                  -> void { addViaCopy(key, value); });
}

void HeaderMapImpl::addViaCopy(const LowerCaseString& key, const std::string& value) {
  addViaMove(LowerCaseString(key), std::string(value));
}

void HeaderMapImpl::addViaMove(LowerCaseString&& key, std::string&& value) {
  headers_.emplace_back(LowerCaseString(std::move(key)), std::move(value));
  ASSERT(key.get().empty() && value.empty());
}

void HeaderMapImpl::addViaMoveValue(const LowerCaseString& key, std::string&& value) {
  addViaMove(LowerCaseString(key), std::move(value));
}

uint64_t HeaderMapImpl::byteSize() const {
  uint64_t total_size = 0;
  for (const HeaderEntry& entry : headers_) {
    total_size += (entry.first.get().size() + entry.second.size());
  }

  return total_size;
}

const std::string& HeaderMapImpl::get(const LowerCaseString& key) const {
  for (const HeaderEntry& entry : headers_) {
    if (entry.first == key) {
      return entry.second;
    }
  }

  return EMPTY_STRING;
}

bool HeaderMapImpl::has(const LowerCaseString& key) const {
  for (const HeaderEntry& entry : headers_) {
    if (entry.first == key) {
      return true;
    }
  }

  return false;
}

void HeaderMapImpl::iterate(ConstIterateCb cb) const {
  for (const HeaderEntry& entry : headers_) {
    cb(entry.first, entry.second);
  }
}

void HeaderMapImpl::replaceViaCopy(const LowerCaseString& key, const std::string& value) {
  replaceViaMove(LowerCaseString(key), std::string(value));
}

void HeaderMapImpl::replaceViaMove(LowerCaseString&& key, std::string&& value) {
  // Nuke any existing values.
  remove(key);
  addViaMove(std::move(key), std::move(value));
}

void HeaderMapImpl::replaceViaMoveValue(const LowerCaseString& key, std::string&& value) {
  replaceViaMove(LowerCaseString(key), std::move(value));
}

void HeaderMapImpl::remove(const LowerCaseString& remove_key) {
  for (auto i = headers_.begin(); i != headers_.end();) {
    auto temp = i;
    i++;
    if (temp->first == remove_key) {
      // Lists have stable iterators so holding onto temp and incrementing i is safe.
      headers_.erase(temp);
    }
  }
}

} // Http
