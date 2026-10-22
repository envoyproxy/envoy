sockets: fixed io_uring shutdown hangs when a peer stops reading or a partial write races with
cancellation, and premature socket close while cancellation completions remain outstanding.
