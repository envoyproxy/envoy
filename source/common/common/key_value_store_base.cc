#include "source/common/common/key_value_store_base.h"

#include <algorithm>
#include <chrono>

#include "absl/cleanup/cleanup.h"

namespace Envoy {
namespace {

// Removes a length prefixed token from |contents| and returns the token,
// or returns absl::nullopt on failure.
absl::optional<absl::string_view> getToken(absl::string_view& contents, std::string& error) {
  const auto it = contents.find('\n');
  if (it == contents.npos) {
    error = "Bad file: no newline";
    return {};
  }
  uint64_t length;
  if (!absl::SimpleAtoi(contents.substr(0, it), &length)) {
    error = "Bad file: no length";
    return {};
  }
  contents.remove_prefix(it + 1);
  if (contents.size() < length) {
    error = "Bad file: insufficient contents";
    return {};
  }
  absl::string_view token = contents.substr(0, length);
  contents.remove_prefix(length);
  return token;
}

bool checkForTtl(absl::string_view& contents) {
  if (absl::StartsWith(contents, KV_STORE_TTL_KEY)) {
    contents.remove_prefix(KV_STORE_TTL_KEY.length());
    return true;
  }
  return false;
}

} // namespace

KeyValueStoreBase::KeyValueStoreBase(Event::Dispatcher& dispatcher,
                                     std::chrono::milliseconds flush_interval, uint32_t max_entries)
    : max_entries_(max_entries), flush_timer_(dispatcher.createTimer([this, flush_interval]() {
        flush();
        flush_timer_->enableTimer(flush_interval);
      })),
      ttl_manager_([this](const std::vector<std::string>& expired) { onExpiredKeys(expired); },
                   dispatcher, dispatcher.timeSource()),
      time_source_(dispatcher.timeSource()) {
  if (flush_interval.count() > 0) {
    flush_timer_->enableTimer(flush_interval);
  }
}

bool KeyValueStoreBase::parseContents(absl::string_view contents) {
  std::string error;
  while (!contents.empty()) {
    absl::optional<absl::string_view> key = getToken(contents, error);
    absl::optional<absl::string_view> value;
    absl::optional<std::chrono::seconds> ttl = absl::nullopt;
    if (key.has_value()) {
      value = getToken(contents, error);
    }
    if (!key.has_value() || !value.has_value()) {
      ENVOY_LOG(warn, error);
      return false;
    }
    if (checkForTtl(contents)) {
      uint64_t ttl_int;
      auto token = getToken(contents, error);
      if (!token.has_value()) {
        ENVOY_LOG(warn, "Failed to read TTL token" + error);
        return false;
      }
      if (!absl::SimpleAtoi(token.value(), &ttl_int)) {
        ENVOY_LOG(warn, "TTL was read from disk but failed to convert to integer");
        return false;
      }
      ttl.emplace(std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::time_point<std::chrono::system_clock>(std::chrono::seconds(ttl_int)) -
          time_source_.systemTime()));
      if (ttl <= std::chrono::seconds(0)) {
        continue;
      }
    }
    addOrUpdate(key.value(), value.value(), ttl);
  }
  return true;
}

void KeyValueStoreBase::addOrUpdate(absl::string_view key_view, absl::string_view value_view,
                                    absl::optional<std::chrono::seconds> ttl) {
  ENVOY_BUG(!under_iterate_, "addOrUpdate under the stack of iterate");
  std::string key(key_view);
  std::string value(value_view);
  // Do not add if ttl is <= 0
  if (ttl && ttl <= std::chrono::seconds(0)) {
    ASSERT(false);
    return;
  }
  absl::optional<std::chrono::seconds> absolute_ttl = absl::nullopt;
  if (ttl) {
    absolute_ttl.emplace(ttl.value() + std::chrono::duration_cast<std::chrono::seconds>(
                                           time_source_.systemTime().time_since_epoch()));
  }

  // Attempt to insert the entry into the store. If it already exists, remove
  // the old entry and insert the new one so it will be in the proper place in
  // the linked list.
  ValueWithTtl value_with_ttl(value, absolute_ttl);
  if (!store_.emplace(key, value_with_ttl).second) {
    store_.erase(key);
    store_.emplace(key, value_with_ttl);
    ttl_manager_.clear(key);
  }
  if (ttl) {
    ttl_manager_.add(std::chrono::milliseconds(ttl.value()), key);
  }
  if (max_entries_ && store_.size() > max_entries_) {
    store_.pop_front();
  }

  if (!flush_timer_->enabled()) {
    flush();
  }
}

void KeyValueStoreBase::onExpiredKeys(const std::vector<std::string>& keys) {
  ENVOY_BUG(!under_iterate_, "onExpiredKeys under the stack of iterate");
  for (const auto& key : keys) {
    store_.erase(std::string(key));
  }
  if (!flush_timer_->enabled()) {
    flush();
  }
}

void KeyValueStoreBase::remove(absl::string_view key) {
  ENVOY_BUG(!under_iterate_, "remove under the stack of iterate");
  ttl_manager_.clear(std::string(key));
  store_.erase(std::string(key));
  if (!flush_timer_->enabled()) {
    flush();
  }
}

absl::optional<absl::string_view> KeyValueStoreBase::get(absl::string_view key) {
  auto it = store_.find(std::string(key));
  if (it == store_.end()) {
    return {};
  }
  return it->second.value_;
}

void KeyValueStoreBase::iterate(ConstIterateCb cb) const {
  under_iterate_ = true;
  absl::Cleanup restore_under_iterate = [this] { under_iterate_ = false; };

  for (const auto& [key, value] : store_) {
    Iterate ret = cb(key, value.value_);
    if (ret == Iterate::Break) {
      return;
    }
  }
}

} // namespace Envoy
