sockets: fixed a crash in the experimental ``io_uring`` socket interface where a full submission
queue during the completion loop aborted the worker, because the retry relied on a submission that
was deferred until the loop finished. The retry now flushes the submission queue directly, and
submission queue polling waits for a free entry. This only affected deployments that opted into
``io_uring``.
