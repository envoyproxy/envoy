Fixed a use-after-free in the ``sip_proxy`` network filter (contrib). The per-worker transaction
table only marked an ended transaction as deleted and removed it later, so an upstream response
with that transaction's branch, arriving on an upstream connection shared with another downstream
connection, was delivered to the freed transaction. Ended transactions are now removed right away,
an upstream request no longer refers to a transaction that has moved away from it or ended, and a
late response after all transactions on an upstream connection have ended is dropped instead of
dereferencing a null pointer.
