build: added the ``//bazel:use_system_allocator`` boolean build setting as an alternative to
``--define tcmalloc=disabled``. This allows Starlark transitions to select the system allocator
for individual targets without changing the allocator for the rest of the build.
