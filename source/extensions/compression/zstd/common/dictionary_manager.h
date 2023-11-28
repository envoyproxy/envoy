#pragma once

#include <functional>

#include "envoy/event/dispatcher.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/config/datasource.h"

#include "zstd.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Zstd {
namespace Common {

// Dictionary manager for `Zstd` compression.
template <class T, size_t (*deleter)(T*), unsigned (*getDictId)(const T*)> class DictionaryManager {
public:
  using DictionaryBuilder = std::function<T*(const void*, size_t)>;

  DictionaryManager(
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::DataSource> dictionaries,
      Event::Dispatcher& dispatcher, Api::Api& api, ThreadLocal::SlotAllocator& tls,
      bool replace_mode, DictionaryBuilder builder)
      : api_(api), tls_slot_(ThreadLocal::TypedSlot<DictionaryThreadLocalMap>::makeUnique(tls)),
        replace_mode_(replace_mode), builder_(builder) {
    bool is_watch_added = false;
    watcher_ = dispatcher.createFilesystemWatcher();

    auto dictionary_map = std::make_shared<DictionaryThreadLocalMap>();
    dictionary_map->reserve(dictionaries.size());

    for (const auto& source : dictionaries) {
      const auto data = Config::DataSource::read(source, false, api);
      auto dictionary = DictionarySharedPtr(builder_(data.data(), data.length()));
      auto id = getDictId(dictionary.get());
      // If id == 0, the dictionary is not conform to Zstd specification, or empty.
      RELEASE_ASSERT(id != 0, "Illegal Zstd dictionary");
      dictionary_map->emplace(id, std::move(dictionary));
      if (source.specifier_case() ==
          envoy::config::core::v3::DataSource::SpecifierCase::kFilename) {
        is_watch_added = true;
        const auto& filename = source.filename();
        watcher_->addWatch(
            filename, Filesystem::Watcher::Events::Modified | Filesystem::Watcher::Events::MovedTo,
            [this, id, filename](uint32_t) { onDictionaryUpdate(id, filename); });
      }
    }

    tls_slot_->set([dictionary_map](Event::Dispatcher&) {
      auto map = std::make_shared<DictionaryThreadLocalMap>();
      map->insert(dictionary_map->begin(), dictionary_map->end());
      return map;
    });

    if (!is_watch_added) {
      watcher_.reset();
    }
  };

  T* getDictionary(bool first_only, unsigned id) {
    auto dictionary_map = tls_slot_->get();

    typename absl::flat_hash_map<unsigned, DictionarySharedPtr>::iterator it;
    if (first_only) {
      it = dictionary_map->begin();
    } else {
      it = dictionary_map->find(id);
    }
    if (it != dictionary_map->end()) {
      return it->second.get();
    }

    return nullptr;
  };

  T* getDictionaryById(unsigned id) { return getDictionary(false, id); };

  T* getFirstDictionary() { return getDictionary(true, 0); };

private:
  class DictionarySharedPtr : public std::shared_ptr<T> {
  public:
    DictionarySharedPtr(T* object) : std::shared_ptr<T>(object, deleter) {}
  };
  class DictionaryThreadLocalMap : public absl::flat_hash_map<unsigned, DictionarySharedPtr>,
                                   public ThreadLocal::ThreadLocalObject {};

  void onDictionaryUpdate(unsigned origin_id, const std::string& filename) {
    auto file_or_error = api_.fileSystem().fileReadToEnd(filename);
    THROW_IF_STATUS_NOT_OK(file_or_error, throw);
    const auto data = file_or_error.value();
    if (!data.empty()) {
      auto dictionary = DictionarySharedPtr(builder_(data.data(), data.length()));
      auto id = getDictId(dictionary.get());
      // Keep origin dictionary if the new is illegal
      if (id != 0) {
        tls_slot_->runOnAllThreads(
            [dictionary = std::move(dictionary), id, origin_id,
             replace_mode = replace_mode_](OptRef<DictionaryThreadLocalMap> dictionary_map) {
              if (replace_mode) {
                dictionary_map->erase(origin_id);
              }
              dictionary_map->emplace(id, dictionary);
            });
      }
    }
  }

  Api::Api& api_;
  ThreadLocal::TypedSlotPtr<DictionaryThreadLocalMap> tls_slot_;
  bool replace_mode_;
  DictionaryBuilder builder_;
  std::unique_ptr<Filesystem::Watcher> watcher_;
};

} // namespace Common
} // namespace Zstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
