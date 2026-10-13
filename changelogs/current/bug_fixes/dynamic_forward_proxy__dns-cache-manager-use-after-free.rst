Fixed a use-after-free crash in the DNS cache manager: the server-wide ``DnsCacheManager``
singleton no longer retains the stats scope of the listener or filter chain that first created a
DNS cache, which could be freed before a later cache miss for a new cache name dereferenced
it.
