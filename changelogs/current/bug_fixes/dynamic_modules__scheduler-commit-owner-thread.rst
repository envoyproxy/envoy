Fixed a crash when a dynamic module committed an event through a scheduler from one of its own threads
while Envoy released the last other reference to the scheduler's owner, for example during a listener
update. The owner's destructor and the module's destroy hook could then run on the module's
thread and cancel callouts or reset streams that belong to the main or worker thread. The scheduler now
hands its reference to the posted event, so the owner is always destroyed on its own thread.
