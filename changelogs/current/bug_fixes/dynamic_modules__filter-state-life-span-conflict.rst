Fixed dynamic module filter state setters, such as ``set_filter_state_bytes``, returning ``true``
for a key already stored at a different life span, for example by another filter at ``Request``
life span. Envoy refused such a write with an ``ENVOY_BUG``, which aborts debug builds. The setters
now return ``false`` and keep the stored value.
