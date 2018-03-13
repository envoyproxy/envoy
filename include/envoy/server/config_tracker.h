#pragma once

#include <functional>
#include <map>
#include <memory>

#include "common/common/non_copyable.h"
#include "common/protobuf/protobuf.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Server {

// TODO doxme
class ConfigTracker {
public:
  // Must not return nullptr
  typedef std::function<ProtobufTypes::MessagePtr()> Cb;
  typedef std::map<std::string, Cb> CbsMap;

  // TODO doxme
  class EntryOwner {
  public:
    using Ptr = std::unique_ptr<EntryOwner>;
    virtual ~EntryOwner() {}
  protected:
    EntryOwner() = default; // A sly way to make this class "abstract"
  };

  virtual ~ConfigTracker() = default;

  // TODO doxme
  virtual const CbsMap& getCallbacksMap() const PURE;
  // TODO doxme
  virtual EntryOwner::Ptr add(std::string key, Cb cb) PURE;
};

} // namespace Server
} // namespace Envoy
