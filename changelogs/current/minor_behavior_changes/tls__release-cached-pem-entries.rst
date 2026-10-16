The process-wide caches of parsed TLS CRLs, trusted CAs, certificate chains and private keys now
erase an entry as soon as the last TLS context referencing it is released, instead of sweeping the
whole cache for released entries each time a new distinct entry is added.
