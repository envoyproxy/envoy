The :ref:`aws_request_signing <config_http_filters_aws_request_signing>` filter now counts requests
that were forwarded unsigned, because the credentials provider chain resolved to no credentials,
with new ``signing_skipped`` and ``payload_signing_skipped`` counters rather than as
``signing_added`` and ``payload_signing_added``. Forwarding those requests unsigned is unchanged and
still intentional, but it was previously indistinguishable in statistics from a request that really
was signed. This behavioral change can be reverted by setting the runtime guard
``envoy.reloadable_features.aws_request_signing_count_skipped_separately`` to ``false``, in which
case the new counters are still emitted but ``signing_added`` and ``payload_signing_added`` keep
their previous totals.
