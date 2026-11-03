sockets: fixed shutdown hangs in the opt-in experimental ``io_uring`` socket interface when a peer
stops reading or a partial write races with cancellation, and premature socket close while cancellation
completions remain outstanding. The write timeout must be greater than zero when configured.
