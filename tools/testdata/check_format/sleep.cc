namespace Envoy {

// Directly calling sleep_for is no good; must inject time system.
int waiting() {
  return std::this_thread::sleep_for(mutex, duration);
}

} // namespace Envoy
