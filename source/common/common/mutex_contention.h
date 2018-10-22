#pragma once

#include <atomic>

namespace Envoy {
namespace Thread {

namespace {
std::atomic<int64_t> num_contentions(0);
std::atomic<int64_t> current_wait_cycles(0);
std::atomic<int64_t> lifetime_wait_cycles(0);
} // namespace

void mutexContentionCallback(const char*, const void*, int64_t wait_cycles) {
  num_contentions += 1;
  current_wait_cycles = wait_cycles;
  lifetime_wait_cycles += wait_cycles;
}

void mutexContentionReset() {
  num_contentions = 0;
  current_wait_cycles = 0;
  lifetime_wait_cycles = 0;
}

int64_t getNumContentions() { return num_contentions; }
int64_t getCurrentWaitCycles() { return current_wait_cycles; }
int64_t getLifetimeWaitCycles() { return lifetime_wait_cycles; }

} // namespace Thread
} // namespace Envoy
