Fixed an integer overflow in the hot restart IPC message length handling. An
attacker-controlled wire length could wrap when computing the receive buffer size,
causing a zero-length allocation and undefined behavior. The fix adds an overflow
check before resizing the buffer.
