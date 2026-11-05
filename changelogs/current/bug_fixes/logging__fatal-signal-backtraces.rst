Fixed fatal signal backtraces re-entering the logger mutex, which could abort or deadlock when the
mutex was already held on the crashing thread or inherited locked across a fork. Native and coroutine
backtraces now write directly to stderr in fatal signal handlers, preserving fault descriptions and
backtrace headers and stack decoder compatibility without using the logging infrastructure.
The alternate signal stack is sized using the runtime ``_SC_SIGSTKSZ`` minimum with headroom for
nested fatal signals.
