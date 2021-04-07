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
   */
  virtual void charge(uint64_t amount) PURE;

  /**
   * Called on to credit the account as
   * a charged resource is no longer used.
   *
   * @param amount the amount to credit.
   */
  virtual void credit(uint64_t amount) PURE;
};

} // namespace Envoy
