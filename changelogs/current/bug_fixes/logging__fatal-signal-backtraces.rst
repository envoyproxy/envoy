Fixed fatal signal backtraces re-entering the logger mutex, which could abort or deadlock when the
mutex was already held on the crashing thread or inherited locked across a fork. Native and coroutine
backtraces now write directly to stderr in fatal signal handlers, preserving fault descriptions and
backtrace headers without using the logging infrastructure.
