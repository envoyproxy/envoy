When ``propagate_response_headers`` is enabled and a filter state object is already present under
``envoy.tcp_proxy.propagate_response_headers``, the captured CONNECT response headers are now written
into that object instead of replacing it, so that anything else holding the object observes the value.
Behavior is unchanged when no such object is present.
