Fixed an out-of-bounds read in the ``redis_proxy`` network filter when a transaction ``WATCH`` command
(or a transaction's first command) is received without a key argument. Such a request is now rejected
with a wrong-number-of-arguments error.
