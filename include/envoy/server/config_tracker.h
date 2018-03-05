#pragma once

#include <functional>
#include <map>
#include <memory>

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {

// TODO doxme
class ConfigTracker {
public:
  using Cb = std::function<ProtobufTypes::MessagePtr()>;
  using CbsMap = std::map<std::string, Cb>;

  // TODO doxme
  class EntryOwner {
  public:
    using Ptr = std::unique_ptr<EntryOwner>;
    virtual ~EntryOwner() {}
    EntryOwner(const EntryOwner&) = delete;                  // Noncopyable
    EntryOwner& operator=(const EntryOwner& other) = delete; // Non-copy-constructible
  protected:
    EntryOwner() = default; // A sly way to make this class "abstract"
  };

  virtual ~ConfigTracker() = default;

  // TODO doxme
  virtual const CbsMap& getCallbacksMap() const = 0;
  // TODO doxme
  virtual EntryOwner::Ptr add(std::string key, Cb cb) = 0;
};

} // namespace Server
} // namespace Envoy
