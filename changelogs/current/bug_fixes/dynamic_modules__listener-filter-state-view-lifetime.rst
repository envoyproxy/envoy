dynamic modules: fixed a use-after-free in the listener filter. Consecutive typed filter-state
getter calls in the same event hook shared a single scratch buffer, so an earlier returned view was
freed by a later call. Each getter now appends to a per-hook scratch that stays valid until the
event hook returns.
