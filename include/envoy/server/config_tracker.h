#pragma once

#include <functional>
#include <map>
#include <memory>

#include "envoy/common/pure.h"

#include "common/common/non_copyable.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {

/**
 * ConfigTracker is used by the `/config_dump` admin endpoint to manage storage of config-providing
 * callbacks with weak ownership semantics. Callbacks added to ConfigTracker only live as long as
 * the returned EntryOwner object (or ConfigTracker itself, if shorter). Keys should be descriptors
 * of the configs provided by the corresponding callback. They must be unique.
 * ConfigTracker is *not* threadsafe.
 */
class ConfigTracker {
public:
  using Cb = std::function<ProtobufTypes::MessagePtr()>;
  using CbsMap = std::map<std::string, Cb>;

  /**
   * EntryOwner supplies RAII semantics for entries in the map.
   * The entry is not removed until the EntryOwner or the ConfigTracker itself is destroyed,
   * whichever happens first. When you add() an entry, you must hold onto the returned
   * owner object for as long as you want the entry to stay in the map.
   */
  class EntryOwner {
  public:
    virtual ~EntryOwner() = default;

  protected:
    EntryOwner() = default; // A sly way to make this class "abstract."
  };
  using EntryOwnerPtr = std::unique_ptr<EntryOwner>;

  virtual ~ConfigTracker() = default;

  /**
   * @return const CbsMap& The map of string keys to tracked callbacks.
   */
  virtual const CbsMap& getCallbacksMap() const PURE;

  /**
   * Add a new callback to the map under the given key
   * @param key the map key for the new callback.
   * @param cb the callback to add. *must not* return nullptr.
   * @return EntryOwnerPtr the new entry's owner object. nullptr if the key is already present.
   */
  virtual EntryOwnerPtr add(const std::string& key, Cb cb) PURE;
};

} // namespace Server
} // namespace Envoy
