dynamic modules: fixed a use-after-free in the stats sink where the histogram name passed to
``on_histogram_complete`` was serialized into a single thread local buffer. A module that recorded
a histogram from inside ``on_histogram_complete`` re-entered the sink on the same thread and
overwrote the name the outer callback was still reading. The outermost call now keeps the shared
buffer while a re-entrant call uses its own buffer.
