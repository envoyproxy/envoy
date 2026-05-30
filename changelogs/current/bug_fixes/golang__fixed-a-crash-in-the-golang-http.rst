Fixed a crash in the Golang HTTP filter introduced by #44503 where ``continueStatus`` or
``sendLocalReply`` invoked synchronously from inside a Go ``OnHttpHeader``/``OnHttpData``
cgo callback would re-enter the C++ state machine while the cgo frame was still on the
stack, tripping the ``ASSERT(filterState() == Processing*)`` at the top of
``handle*GolangStatus`` once the cgo call returned. The inline-on-worker-thread
optimization from #44503 is reverted for these two CAPI methods; both now always
post to the dispatcher, matching the pre-#44503 behavior.
