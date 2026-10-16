Fixed internal listener connections retaining response buffers when a blocked write flush
outlived its peer. Closing a user-space socket now wakes its peer's pending write so the
connection can finish closing.
