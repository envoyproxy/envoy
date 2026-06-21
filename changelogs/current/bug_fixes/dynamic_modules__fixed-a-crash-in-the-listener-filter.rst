Fixed a crash in the listener dynamic module filter where a module that issued an HTTP callout
aborted the worker. The callout path called ``shared_from_this`` on a filter that was not shared
owned, which threw ``std::bad_weak_ptr``. The filter is now held by a ``shared_ptr`` so the async
callout and scheduler paths work.
