Fixed double counting of upstream wire bytes for TCP-over-HTTP tunnels. The HTTP codec now owns
upstream byte accounting, including buffered early data, while raw TCP accounting is unchanged.
