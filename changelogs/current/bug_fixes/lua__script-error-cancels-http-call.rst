Fixed the Lua filter leaving an ``httpCall()`` in flight when the script for the other direction
raised an error, which could call back into the filter after it was destroyed.
