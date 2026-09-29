A hot restart child now waits at most 5 seconds for the parent to answer a listen socket request (a listener added
while the parent is draining) before writing the parent off and binding its own socket, rather than the 30 seconds
allowed for the stats exchange, so a parent whose main thread has stopped answering cannot stall the child's main
thread for that long. A UDP socket the child binds after writing the parent off now starts paused until the parent
is gone, as an inherited one does, since the parent may still be serving that address; a parent that answered that
it has no socket for the address leaves the new socket reading from the start.
