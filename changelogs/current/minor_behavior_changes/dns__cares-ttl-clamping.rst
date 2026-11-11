The c-ares resolver (c-ares 1.34.9) now clamps DNS record TTLs that do not fit in a signed 32-bit
integer to its maximum value (2147483647 seconds) instead of letting them wrap to a negative value,
which Envoy previously treated as a zero TTL. When ``respect_dns_ttl`` is enabled, such records are
now cached for the clamped TTL rather than refreshed on the next cycle.
