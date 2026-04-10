**Summary of changes**:

- Fixed a crash on listener removal with a process-level access log rate limiter
- Dynamic module filters could send incomplete request/response bodies when adjacent filters in the chain performed buffering.
- Internal redirect logic could hang a request when the request buffer overflows.
- Update/fix Docker release images.
- Updates to stats.
