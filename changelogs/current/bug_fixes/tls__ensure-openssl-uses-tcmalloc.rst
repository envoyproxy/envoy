Fixed a bug where OpenSSL was using glibc's allocator instead of tcmalloc. This
resulted in OpenSSL operating on a completely separate heap, defeating
tcmalloc's performance benefits on the TLS hot path and making all OpenSSL
allocations invisible to tcmalloc heap profiling and memory dumps.
