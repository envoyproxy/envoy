.. _arch_overview_listener_filters:

Listener filters
================

As discussed in the :ref:`listener <arch_overview_listeners>` section, listener filters may be
used to manipulate connection metadata. The main purpose of listener filters is to make adding
further system integration functions easier by not requiring changes to Envoy core functionality,
and also make interaction between multiple such features more explicit.

The API for listener filters is relatively simple since ultimately these filters operate on newly
accepted sockets. Filters in the chain can stop and subsequently continue iteration to
further filters. This allows for more complex scenarios such as calling a :ref:`rate limiting
service <arch_overview_rate_limit>`, etc. Envoy already includes several listener filters that
are documented in this architecture overview as well as the :ref:`configuration reference
<config_listener_filters>`.
