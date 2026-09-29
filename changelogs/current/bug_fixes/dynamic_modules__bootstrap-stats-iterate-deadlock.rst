dynamic modules: fixed a main thread deadlock in the bootstrap stats iterate callbacks. They
invoked the module iterator while holding the stats allocator lock, so a stats call from the
iterator re-entered the non-recursive lock and hung the process. The counters and gauges are now
snapshotted before the iterator runs, so it runs without the lock held, and a ``Stop`` return now
ends iteration.
