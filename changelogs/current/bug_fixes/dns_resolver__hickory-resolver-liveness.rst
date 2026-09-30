Fixed a use-after-free in the Hickory DNS resolver. A resolution completing after the resolver was
destroyed dereferenced the freed resolver before checking whether it was still alive, for example
when a module task outlived the shutdown timeout. The resolver now hands the module a never-reused
token instead of its own pointer and resolves that token under a lock before touching any resolver
state, so a late completion is dropped safely.
