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
 * ConfigTracker is used by the `/config_dump` admin endpoint to manage storage of
 * config-providing callbacks with weak ownership semantics. Callbacks added to ConfigTracker
 * only live as long as the returned EntryOwner object (or ConfigTracker itself, if shorter).
 * The advantages are twofold:
 *   1. callbacks can safely operate on data whose lifetimes are known to be bounded by the lifetime
 *      of the owner object. For instance, a callback can capture `this` if the owner is also stores
 *      by `*this`
 *   2. consumers of `getCallbacksMap()` don't have to worry about checking whether tracked
 *      callbacks are still valid. c.f. locking weak_ptrs in a similar but less managed scenerio
 *
 *  Keys should be descriptors of the configs provided by the corresponding callback. They must be
 *  unique.
 *
 *  ConfigTracker is *not* threadsafe. In the context of Envoy's Admin object, it should only be
 *  used in the main thread. *This includes destruction of owner objects.*
 */
class ConfigTracker {
public:
  typedef std::function<ProtobufTypes::MessagePtr()> Cb;
  typedef std::map<std::string, Cb> CbsMap;

  /**
   * EntryOwner supplies RAII semantics for entries in the map.
   * The entry is not removed until the EntryOwner or the ConfigTracker itself is destroyed,
   * whichever happens first. When you add() an entry, you must hold onto the returned
   * owner object for as long as you want the entry to stay in the map.
   */
  class EntryOwner {
  public:
    using Ptr = std::unique_ptr<EntryOwner>;
    virtual ~EntryOwner() {}

  protected:
    EntryOwner() = default; // A sly way to make this class "abstract."
  };

  virtual ~ConfigTracker() = default;

  /**
   * @return const CbsMap& The map of string keys to tracked callbacks.
   */
  virtual const CbsMap& getCallbacksMap() const PURE;

  /**
   * Add a new callback to the map under the given key
   * @param key the map key for the new callback.
   * @param cb the callback to add. *must not* return nullptr.
   * @return EntryOwner::Ptr the new entry's owner object. nullptr if the key is already present.
   */
  virtual EntryOwner::Ptr add(std::string key, Cb cb) PURE;
};

} // namespace Server
} // namespace Envoy
