Fixed a leaked upstream stream and a use-after-free crash when an upstream HTTP filter sent the
response itself through ``encodeHeaders()`` / ``encodeData()`` after the connection pool had
attached a connection. The codec stream stayed open and kept its connection, and a later close of
that connection called into the destroyed upstream request. The upstream codec filter now resets
an unfinished codec stream when the upstream filter chain is destroyed.
