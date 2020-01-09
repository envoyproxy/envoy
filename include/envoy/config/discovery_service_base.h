#pragma once

#include "envoy/config/subscription.h"

namespace Envoy {
namespace Config {

class RdsRouteConfigSubscriptionBase : public Config::SubscriptionCallbacks {};
class ScopedRdsConfigSubscriptionBase : public Config::SubscriptionCallbacks {};
class VhdsSubscriptionBase : public Config::SubscriptionCallbacks {};
class RtdsSubscriptionBase : public Config::SubscriptionCallbacks {};
class SdsApiBase : public Config::SubscriptionCallbacks {};
class CdsApiBase : public Config::SubscriptionCallbacks {};
class EdsClusterBase : public Config::SubscriptionCallbacks {};
class LdsApiBase : public Config::SubscriptionCallbacks {};

} // namespace Config
} // namespace Envoy