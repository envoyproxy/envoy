Fixed a bug in the local rate limiter where an exhausted ``shadow_mode: true`` descriptor would
short-circuit descriptor evaluation and prevent the remaining enforced descriptors and the default
token bucket from being consumed. Requests that were previously allowed by this bypass may now be
rate limited. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.local_ratelimit_shadow_mode_no_short_circuit`` to ``false``.
