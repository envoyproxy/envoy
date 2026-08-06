Added the ``envoy.load_balancing_policies.header_order`` load balancing policy and
``envoy.retry_priorities.header_order`` retry priority extension. These extensions select
upstream endpoints based on an ordered list of priority tiers read from dynamic metadata,
supporting per-request backend ordering for both initial attempts and retries.
