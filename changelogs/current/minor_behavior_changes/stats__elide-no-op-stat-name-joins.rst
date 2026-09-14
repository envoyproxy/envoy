Stat-name construction no longer allocates when a join has at most one non-empty operand. Joining
a name with an empty name produces bytes identical to that name, so ``TagStatNameJoiner`` and the
HTTP response-code stat helpers now reference the non-empty name directly instead of allocating a
byte-identical copy. The router, ext_authz and ratelimit all charge response-code stats with an
empty prefix, so this removes four heap allocations per upstream response. The resulting stat
names are unchanged.
