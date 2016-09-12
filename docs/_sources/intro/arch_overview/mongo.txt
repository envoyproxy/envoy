.. _arch_overview_mongo:

MongoDB
=======

Envoy supports a network level MongoDB sniffing filter with the following features:

* MongoDB wire format BSON parser.
* Detailed MongoDB query/operation statistics including timings and scatter/multi-get counts for
  routed clusters.
* Query logging.
* Per callsite statistics via the $comment query parameter.

The MongoDB filter is a good example of Envoy’s extensibility and core abstractions. At Lyft we use
this filter between all applications and our databases. It provides an invaluable source of data
that is agnostic to the application platform and specific MongoDB driver in use.

MongoDB proxy filter :ref:`configuration reference <config_network_filters_mongo_proxy>`.
