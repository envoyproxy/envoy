#pragma once

#include "common/callback/callback.h"
#include "common/common/logger.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Init {

/**
 * A Caller notifies a Receiver when something has initialized. This works at two levels: first,
 * each initialization target holds a Caller to notify the init manager's Receiver when the target
 * has initialized. And second, the manager holds a Caller to notify its client's Receiver when all
 * of its targets have initialized.
 *
 * Caller and Receiver are simple wrappers for their counterparts in Common::Callback with logging
 * added to help debug initialization and destruction ordering issues that occasionally arise.
 */
class Caller : Logger::Loggable<Logger::Id::init> {
public:
  Caller() = default;
  explicit operator bool() const;
  void operator()() const;

private:
  friend class Receiver;
  Caller(Common::Callback::Caller<> caller, absl::string_view name,
         absl::string_view receiver_name);

  Common::Callback::Caller<> caller_;
  std::string name_;
  std::string receiver_name_;
};

class Receiver : Logger::Loggable<Logger::Id::init> {
public:
  Receiver() = default;
  Receiver(absl::string_view name, std::function<void()> fn);
  ~Receiver();
  Caller caller(absl::string_view name) const;
  void reset();

private:
  std::string name_;
  Common::Callback::Receiver<> receiver_;
};

/**
 * A TargetCaller notifies a TargetReceiver when the target should start initialization. The
 * TargetReceiver accepts a Caller, so that the target can notify the manager when it has
 * initialized.
 *
 * As above, TargetCaller and TargetReceiver are simple wrappers for their counterparts in
 * Common::Callback with logging added.
 */
class TargetCaller : Logger::Loggable<Logger::Id::init> {
public:
  TargetCaller() = default;
  explicit operator bool() const;
  void operator()(const Receiver& receiver) const;

private:
  friend class TargetReceiver;
  TargetCaller(Common::Callback::Caller<Caller> caller, absl::string_view name,
               absl::string_view receiver_name);

  Common::Callback::Caller<Caller> caller_;
  std::string name_;
  std::string receiver_name_;
};

class TargetReceiver : Logger::Loggable<Logger::Id::init> {
public:
  TargetReceiver() = default;
  TargetReceiver(absl::string_view name, std::function<void(Caller)> fn);
  ~TargetReceiver();
  TargetCaller caller(absl::string_view name) const;
  void reset();
  absl::string_view name() const;

private:
  std::string name_;
  Common::Callback::Receiver<Caller> receiver_;
};

} // namespace Init
} // namespace Envoy
