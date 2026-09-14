The ``%COALESCE()%`` access log operator now returns the first result that is present, including a
result that is present but empty. Previously an operator producing an empty value was treated as if
it had produced no value at all, and the next operator in the list was evaluated. This change can be
reverted by setting the runtime guard
``envoy.reloadable_features.coalesce_formatter_accept_empty_values`` to ``false``.
