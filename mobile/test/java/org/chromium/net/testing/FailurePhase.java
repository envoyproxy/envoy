package org.chromium.net.testing;

public enum FailurePhase {
  START,
  READ_SYNC,
  READ_ASYNC;

  @Override
  public String toString() {
    return name().toLowerCase();
  }
}
