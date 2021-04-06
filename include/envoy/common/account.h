#pragma once

#include <functional>
#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {

class Charge;

/*
 * An interface for accounting the usage of a resource.
 * The "resource" tracked here is implicit, if in the future we want to track multiple
 * resources, we should expand the interface to make it explicit.
 *
 * Currently this is only used by L7 streams to track the amount of memory used
 * by the stream.
 *
 */
class Account {
public:
  virtual ~Account() = default;

  /**
   * Returns the outstanding balance.
   */
  virtual uint64_t balance() const PURE;

  /**
   * Charges the account the specified amount.
   *
   * @param amount the amount to debit.
   * @return the charge object that when destroyed, will credit the account
   * freeing the resource.
   */
  virtual std::unique_ptr<Charge> charge(uint64_t amount) PURE;

private:
  friend Charge;

  /**
   * Called by `Charge` on destruction to credits the account as
   * the used resource is freed.
   *
   * @param amount the amount to credit.
   */
  virtual void credit(uint64_t amount) PURE;
};

/**
 * A RAII-like object charging for the usage of a particular resource.
 * It is move only and credits the given Account on destruction. This ensures
 * that an Account never underflows and is only credited once for releasing a resource.
 */
class Charge final {
public:
  Charge(Charge&&) = default;
  Charge& operator=(Charge&& other) = default;
  ~Charge() {
    if (!account_.expired()) {
      account_.lock()->credit(amount_);
    }
  }

  Charge(std::weak_ptr<Account> account, uint64_t amount) : account_(account), amount_(amount) {}

private:
  // Sometimes the given account might no longer be available.
  std::weak_ptr<Account> account_;
  uint64_t amount_;
};

} // namespace Envoy
