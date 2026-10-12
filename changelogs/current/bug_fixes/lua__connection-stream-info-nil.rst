Hardened the ``lua`` filter to avoid a crash in ``connectionStreamInfo()`` when the downstream
connection is absent. The call now returns ``nil`` to the script instead.
