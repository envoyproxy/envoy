Fixed jemalloc memory accounting so that reserved memory includes retained pages, matching tcmalloc
semantics and preventing underflow in physical memory calculations.
