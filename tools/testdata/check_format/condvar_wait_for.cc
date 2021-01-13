namespace Envoy {

// Directly calling waitFor on a condvar no good; need to inject TimeSystem.
int waiting() {
  return condvar.waitFor(mutex, duration);
}

} // namespace Envoy
