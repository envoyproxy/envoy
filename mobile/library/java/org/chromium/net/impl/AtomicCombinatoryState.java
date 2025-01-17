package org.chromium.net.impl;

import java.util.concurrent.atomic.AtomicInteger;

/**
 * "Compare And Swap" logic based class providing a mean to ensure that the last awaited
 * "stateEvent" will be identified as so. Typically a "stateEvent" is a single bit flip.
 *
 * <p>This class is Thread Safe.
 */
final class AtomicCombinatoryState {

  private final int mFinalState;
  private final AtomicInteger mState = new AtomicInteger(0);

  /**
   * finalState must be a power of two minus one: 1, 3, 7, 15, ...
   */
  AtomicCombinatoryState(int finalState) {
    assert finalState > 0 && ((finalState + 1) & finalState) == 0;
    this.mFinalState = finalState;
  }

  /**
   * Returns true if the state reaches, for the first time, the final state. The provided stateEvent
   * is atmomically ORed with the current state - the outcome is saved as the new state.
   */
  boolean hasReachedFinalState(int stateEvent) {
    assert stateEvent <= mFinalState;
    while (true) {
      int originalState = mState.get();
      int updatedState = originalState | stateEvent;
      if (mState.compareAndSet(originalState, updatedState)) {
        return originalState != mFinalState && updatedState == mFinalState;
      }
    }
  }
}
