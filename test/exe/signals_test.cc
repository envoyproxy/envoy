#include <signal.h>
#include <sys/mman.h>

#include "exe/signal_action.h"

#include "gtest/gtest.h"

TEST(Signals, InvalidAddressDeathTest) {
  SignalAction actions;
  EXPECT_DEATH([]() -> void {
    // Oooooops!
    volatile int* nasty_ptr = reinterpret_cast<int*>(0x0);
    *(nasty_ptr) = 0;
  }(), "Segmentation fault");
}

#if defined(__x86_64__) || defined(__i386__)
// Unfortunately we don't have a reliable way to do this on other platforms
TEST(Signals, IllegalInstructionDeathTest) {
  SignalAction actions;
  EXPECT_DEATH([]() -> void {
    // Intel defines the "ud2" opcode to be an invalid instruction:
    __asm__("ud2");
  }(), "Illegal");
}
#endif

TEST(Signals, AbortDeathTest) {
  SignalAction actions;
  EXPECT_DEATH([]() -> void { abort(); }(), "Aborted");
}

TEST(Signals, BadMathDeathTest) {
  SignalAction actions;
  EXPECT_DEATH([]() -> void {
    // It turns out to be really hard to not have the optimizer get rid of a
    // division by zero.  Just raise the signal for this test.
    raise(SIGFPE);
  }(), "Floating point");
}

TEST(Signals, BusDeathTest) {
  SignalAction actions;
  EXPECT_DEATH([]() -> void {
    // Bus error is tricky.  There's one way that can work on POSIX systems
    // described below but it depends on mmaping a file. Just make it easy and
    // raise a bus.
    //
    // FILE *f = tmpfile();
    // int *p = mmap(0, 4, PROT_WRITE, MAP_PRIVATE, fileno(f), 0);
    // *p = 0;
    raise(SIGBUS);
  }(), "Bus");
}

TEST(Signals, LegalTest) {
  // Don't do anything wrong.
  { SignalAction actions; }
  // Nothing should happen...
}

TEST(Signals, RaiseNonFatalTest) {
  {
    SignalAction actions;
    // I urgently request that you do nothing please!
    raise(SIGURG);
  }
  // Nothing should happen...
}
