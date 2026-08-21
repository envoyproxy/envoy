Fixed a data race and cross-thread resolver sharing introduced with the
``envoy.restart_features.shared_cares_dns_resolver`` runtime guard. The shared resolver cache lived
on the process wide c-ares DNS resolver factory and was consulted by every caller, without
synchronization. Callers that create resolvers on worker thread could therefore race on the
cache, and could be handed a resolver bound to another thread's dispatcher. Moving the shared logic
into the upstream cluster similar to the default shared resolver eliminates any future issues and
avoid locking in the worker thread. Also switch default to for
``envoy.restart_features.shared_cares_dns_resolver`` to false.

