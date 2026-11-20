``Upstream::RetryPriorityFactory::createRetryPriority`` has been replaced by
``Upstream::RetryPriorityFactory::createRetryPriorityFactory``, which validates retry priority
configuration at config load time and returns ``absl::StatusOr<Upstream::RetryPriorityFactoryCb>``.
``PreviousPrioritiesRetryPriorityFactory`` now validates ``PreviousPrioritiesConfig`` (such as
``update_frequency > 0``) when the route configuration is loaded instead of throwing an uncaught
exception on the worker data path during the first matching request. This validation behavior can
be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.validate_retry_priority_config_at_load`` to ``false``.
