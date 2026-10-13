namespace Envoy {

int waiting() { return timeSystem().waitFor(mutex, condvar, duration); }

} // namespace Envoy
