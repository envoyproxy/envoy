dynamic modules: fixed a use-after-free where the cluster load balancer typed filter state getter
returned a view backed by a single thread local string. A module that read two keys during a host
selection callback saw the first view clobbered by the second read. Serialized values are now
retained for the whole host selection callback and cleared when it returns.
