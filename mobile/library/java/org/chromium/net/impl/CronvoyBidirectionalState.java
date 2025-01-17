package org.chromium.net.impl;

import androidx.annotation.IntDef;

import org.chromium.net.RequestFinishedInfo;
import org.chromium.net.impl.CronvoyRequestFinishedInfoImpl.FinishedReason;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Holder the the current state associated to a bidirectional stream. The main goal is to provide
 * a mean to determine what should be the next action for a given event by considering the
 * current state. This class uses Compare And Swap logic. The next state is saved with
 * {@code AtomicInteger.compareAndSet()}.
 *
 * <p>All methods in this class are Thread Safe.
 *
 * <p><b>WRITE state diagram</b>
 * <li>There are 11 states represented by 5 State bits.
 * <li>The USER_WRITE event can occur on any state - it does not change the state. However, if
 * attempted after a USER_LAST_WRITE event, the this will throw an Exception. It is absent from
 * the diagram.
 * <li>The WRITE_COMPLETED event does not change the state and is therefore absent from the diagram.
 * <li>The USER_FLUSH event won't change the state if the request headers have been sent.
 * <li>The READY_TO_FLUSH event will not change the state if the current state is "Busy" or
 * "BusyAndEnding" (in general, if the state bit WAITING_FOR_FLUSH is false.)
 *
 * <p><pre>
 * Write State                State bits use to represent the write state
 * -----------                -------------------------------------------
 * Starting:                  []
 * Ending:                    [END_STREAM_WRITTEN]
 * ReadyWaitHeaders:          [WAITING_FOR_FLUSH]
 * Ready:                     [WAITING_FOR_FLUSH, HEADERS_SENT]
 * ReadyWaitHeadersAndEnding: [WAITING_FOR_FLUSH, END_STREAM_WRITTEN]
 * WaitHeaders:               [WAITING_FOR_FLUSH, END_STREAM_WRITTEN, DONE]
 * ReadyAndEnding:            [WAITING_FOR_FLUSH, END_STREAM_WRITTEN,
 *                             HEADERS_SENT]
 * Busy:                      [WRITING, HEADERS_SENT]
 * BusyAndEnding:             [WRITING, HEADERS_SENT, END_STREAM_WRITTEN]
 * WaitingDone:               [END_STREAM_WRITTEN, HEADERS_SENT]
 * WriteDone:                 [WRITE_DONE, END_STREAM_WRITTEN, HEADERS_SENT]
 *
 *
 * |-------------|     USER_START_    |-----------| <-- LAST_WRITE_COMPLETED --
 * |  Starting   | -- WITH_HEADERS -> | WriteDone | <---------------          |
 * |-------------|     _READ_ONLY     |-----------| <---------     |          |
 *  |     |  |  |                                            |     |          |
 *  |     |  |  -- USER_START_READ_ONLY --                   |     |          |
 *  |     |  |                           V                   |     |          |
 *  |     |  |                |-------------| -- USER_FLUSH --     |          |
 *  |     |  |                | WaitHeaders |                      |          |
 *  |     |  |                |-------------| <--------            |          |
 *  |     |  |                                        |            |          |
 *  |     |  -- USER_LAST_WRITE ---                   |            |          |
 *  |     |                       V                   |            |          |
 *  |     |                  |--------| -- USER_START_READ_ONLY    |          |
 *  |     |                  | Ending | -- USER_START_WITH_HEADERS_READ_ONLY  |
 *  |  USER_START            |--------| -- USER_START_WITH_HEADERS            |
 *  |     |                       |                        |                  |
 *  |     V                       -- USER_START --         V                  |
 *  |  |------------------|                      |  |----------------|        |
 *  |  | ReadyWaitHeaders | -- USER_LAST_WRITE   |  | ReadyAndEnding | --     |
 *  |  |------------------| --        |          |  |----------------|  |     |
 *  |                        |        |          |                      |     |
 * USER_START_WITH_HEADERS   |        V          V                      |     |
 *    |                      |  |---------------------------|           |     |
 *    |  ------------------->|  | ReadyWaitHeadersAndEnding | --------->|     |
 *    V  |                   |  |---------------------------|   |       |     |
 * |-------| <--USER_FLUSH ---                                  |       |     |
 * | Ready | ------------------------ USER_LAST_WRITE ----      |  USER_FLUSH |
 * |-------| <--------                                   |      |       |     |
 *    |              |                                   V      |       |     |
 *    |              |         READY_TO_FLUSH ---- |----------------|   |     |
 * READY_TO_FLUSH    |             |               | ReadyAndEnding | <--     |
 *    |              |             V           --> |----------------|         |
 *    V              |     |---------------|   |              |               |
 * |------|          |     | BusyAndEnding |   |      READY_TO_FLUSH_LAST     |
 * | Busy |          |     |---------------|   |              V               |
 * |______|          |             |           |       |-------------|        |
 *    |              |      ON_SEND_WINDOW_AVAILABLE   | WaitingDone | --------
 * ON_SEND_WINDOW_AVAILABLE                            |-------------|
 * </pre>
 *
 * <p><b>READ state diagram</b>
 * <li>There are 16 states represented by 7 State bits.
 * <li>Some "read" related events don't change the state, like "ON_DATA". Those are omitted.
 * <li>There is something very peculiar about the "last read". When EM indicates that there is no
 * more data to receive, then the END_STREAM_READ state bit is set to one, as expected. However, if
 * the last ByteBuffer received is not empty, or if the response is "body less", then the final
 * "read" loop must be faked: the final read must return zero bytes by contract.
 *
 * <p><pre>
 * Read State                      State bits use to represent the read state
 * ----------                      ------------------------------------------
 * Starting:                       []
 * ReadyWaitingHeadersAndStreamOk: [WAITING_FOR_READ]
 * ReadyWaitingHeaders:            [WAITING_FOR_READ, STREAM_READY_EXECUTED]
 * Postponed:                      [READ_POSTPONED]
 * ReadyWaitingStreamOk:           [WAITING_FOR_READ, HEADERS_RECEIVED]
 * ReadyWaitingStreamOkLast:       [WAITING_FOR_READ, HEADERS_RECEIVED,
 *                                  END_STREAM_READ]
 * PostponedWaitingHeaders:        [READ_POSTPONED, STREAM_READY_EXECUTED]
 * PostponedWaitingStreamOk:       [READ_POSTPONED, HEADERS_RECEIVED]
 * PostponedWaitingStreamOkLast:   [READ_POSTPONED, HEADERS_RECEIVED,
 *                                  END_STREAM_READ]
 * PostponeReady:                  [READ_POSTPONED, STREAM_READY_EXECUTED,
 *                                  HEADERS_RECEIVED]
 * PostponeReadyLast:              [READ_POSTPONED, STREAM_READY_EXECUTED,
 *                                  HEADERS_RECEIVED, END_STREAM_READ]
 * Ready:                          [WAITING_FOR_READ, HEADERS_RECEIVED,
 *                                  STREAM_READY_EXECUTED]
 * ReadyLast:                      [WAITING_FOR_READ, HEADERS_RECEIVED,
 *                                  STREAM_READY_EXECUTED, END_STREAM_READ]
 * Reading:                        [READING, HEADERS_RECEIVED,
 *                                  STREAM_READY_EXECUTED]
 * ReadingLast:                    [READING, HEADERS_RECEIVED,
 *                                  STREAM_READY_EXECUTED, END_STREAM_READ]
 * ReadDone:                       [STREAM_READY_EXECUTED, END_STREAM_READ,
 *                                  READ_DONE]
 *
 *
 *    |-------------| -- USER_START* --> |--------------------------------|
 *    |  Starting   |                    | ReadyWaitingHeadersAndStreamOk |
 *    |-------------|    --------------- |--------------------------------|
 *                       |                  |                    |
 *    STREAM_READY_CALLBACK_DONE           READ              ON_HEADERS*
 *                       |                  |                    |
 *                       V                  V                    V
 *    |---------------------|      |-----------|   |--------------------------|
 *    | ReadyWaitingHeaders |      | Postponed |   | ReadyWaitingStreamOk or  |
 *    |---------------------|      |-----------|   | ReadyWaitingStreamOkLast |
 *     |    |                       |         |    |--------------------------|
 *     |    |                       |         |               |              |
 *     |   READ  STREAM_READY_CALLBACK_DONE  ON_HEADERS*     READ            |
 *     |    |                       |         |               |              |
 *     |    V                       V         V               V              |
 *     |   |-------------------------|   |------------------------------|    |
 *     |   | PostponedWaitingHeaders |   | PostponedWaitingStreamOk or  |    |
 *     |   |-------------------------|   | PostponedWaitingStreamOkLast |    |
 *     |                |                |------------------------------|    |
 *     |                |                       |                            |
 *  ON_HEADERS*     ON_HEADERS*       STREAM_READY_CALLBACK_DONE             |
 *     |                |                       |                            |
 *     V                V                       |                            |
 * |-----------|   |-------------------|        |                            |
 * | Ready or  |   | PostponeReady or  | <-------              |----------|  |
 * | ReadyLast |   | PostponeReadyLast |                       | ReadDone |  |
 * |-----------|   |-------------------|                       |----------|  |
 *  ^   ^   |                    |                                      ^    |
 *  |   |   |     READY_TO_START_POSTPONED_READ_IF_ANY                  |    |
 *  |   |   |                    |                                      |    |
 *  |   |   |                    V                                      |    |
 *  |   |   |                |-------------| -- LAST_READ_COMPLETED -----    |
 *  |   |   ----- READ ----> | Reading or  | -- ON_DATA_END_STREAM ---       |
 *  |   |                    | ReadingLast |  Reading -> ReadingLast |       |
 *  |   -- READ_COMPLETED -- |-------------| <------------------------       |
 *  |                                                                        |
 *  -------------------------------------------- STREAM_READY_CALLBACK_DONE --
 * </pre>
 */
final class CronvoyBidirectionalState {

  /**
   * Enum of the events altering the global state. There are 3 types of events: User induced
   * (prefixed with USER_), EM Callbacks (prefixed with ON_), and internal events (the remaining
   * ones).
   */
  @IntDef({
      Event.USER_START,
      Event.USER_START_WITH_HEADERS,
      Event.USER_START_READ_ONLY,
      Event.USER_START_WITH_HEADERS_READ_ONLY,
      Event.USER_WRITE,
      Event.USER_LAST_WRITE,
      Event.USER_FLUSH,
      Event.USER_READ,
      Event.USER_CANCEL,
      Event.ON_SEND_WINDOW_AVAILABLE,
      Event.ON_HEADERS,
      Event.ON_HEADERS_END_STREAM,
      Event.ON_DATA,
      Event.ON_DATA_END_STREAM,
      Event.ON_TRAILERS,
      Event.ON_COMPLETE,
      Event.ON_CANCEL,
      Event.ON_ERROR,
      Event.ERROR,
      Event.STREAM_READY_CALLBACK_DONE,
      Event.READY_TO_FLUSH,
      Event.READY_TO_FLUSH_LAST,
      Event.WRITE_COMPLETED,
      Event.READY_TO_START_POSTPONED_READ_IF_ANY,
      Event.READ_COMPLETED,
      Event.LAST_WRITE_COMPLETED,
      Event.LAST_READ_COMPLETED,
      Event.READY_TO_FINISH,
  })
  @Retention(RetentionPolicy.SOURCE)
  @interface Event {
    int USER_START = 0; // Ready. Don't send request headers yet. There will be a request body.
    int USER_START_WITH_HEADERS = 1; // Ready to send request headers. There will be a request body.
    int USER_START_READ_ONLY = 2;    // Ready. Don't send request headers yet. No request body.
    int USER_START_WITH_HEADERS_READ_ONLY = 3; // Ready to send request headers. No request body.
    int USER_WRITE = 4;      // User adding a ByteBuffer in the pending queue - not the last one.
    int USER_LAST_WRITE = 5; // User adding a ByteBuffer in the pending queue - that's the last one.
    int USER_FLUSH = 6;      // User requesting to push the pending buffers/headers on the wire.
    int USER_READ = 7;       // User requesting to read the next chunk from the wire.
    int USER_CANCEL = 8;     // User requesting to cancel the stream.
    int ON_SEND_WINDOW_AVAILABLE = 9; // EM invoked the "onSendWindowAvailable" callback.
    int ON_HEADERS = 10;            // EM invoked the "onHeaders" callback - response body to come.
    int ON_HEADERS_END_STREAM = 11; // EM invoked the "onHeaders" callback - no response body.
    int ON_DATA = 12;            // EM invoked the "onData" callback - not last "onData" callback.
    int ON_DATA_END_STREAM = 13; // EM invoked the "onData" callback - final "onData" callback.
    int ON_TRAILERS = 14;        // EM invoked the "onTrailers" callback.
    int ON_COMPLETE = 15;        // EM invoked the "onComplete" callback.
    int ON_CANCEL = 16;          // EM invoked the "onCancel" callback.
    int ON_ERROR = 17;           // EM invoked the "onError" callback.
    int ERROR = 18;              // A fatal error occurred. Can be an internal, or user related.
    int STREAM_READY_CALLBACK_DONE = 19; // Callback.streamReady() was executed.
    int READY_TO_FLUSH = 20; // Internal Event indicating readiness to write the next ByteBuffer.
    int READY_TO_FLUSH_LAST = 21; // Internal Event indicating readiness to write last ByteBuffer.
    int WRITE_COMPLETED = 22; // Internal event indicating to tell the user about a completed write.
    int READY_TO_START_POSTPONED_READ_IF_ANY = 23; // Internal event. The Enum name says it all...
    int READ_COMPLETED = 24; // Internal event indicating to tell the user about a completed read.
    int LAST_WRITE_COMPLETED = 25; // Internal event indicating to tell the user about final write.
    int LAST_READ_COMPLETED = 26;  // Internal event indicating to tell the user about final read.
    int READY_TO_FINISH = 27;      // Internal event indicating to tell the user about success.
  }

  /**
   * Enum of the Next Actions to be taken.
   *
   * <p>There are two types of "NextAction": the ones requesting to notify the user, and the
   * internal ones. For the User notifications, "Schedule" means that the Network Thread is posting
   * a task that will perform the notification, and "Execute" means that the logic is already
   * running under a Thread specified by the User - the notification is executed directly.
   */
  @IntDef({NextAction.NOTIFY_USER_STREAM_READY, NextAction.NOTIFY_USER_HEADERS_RECEIVED,
           NextAction.NOTIFY_USER_WRITE_COMPLETED, NextAction.NOTIFY_USER_READ_COMPLETED,
           NextAction.NOTIFY_USER_TRAILERS_RECEIVED, NextAction.NOTIFY_USER_SUCCEEDED,
           NextAction.NOTIFY_USER_NETWORK_ERROR, NextAction.NOTIFY_USER_FAILED,
           NextAction.NOTIFY_USER_CANCELED, NextAction.WRITE, NextAction.CHAIN_NEXT_WRITE,
           NextAction.FLUSH_HEADERS, NextAction.SEND_DATA, NextAction.READ,
           NextAction.POSTPONE_READ, NextAction.INVOKE_ON_READ_COMPLETED, NextAction.CANCEL,
           NextAction.CARRY_ON, NextAction.TAKE_NO_MORE_ACTIONS})
  @Retention(RetentionPolicy.SOURCE)
  @interface NextAction {
    int NOTIFY_USER_STREAM_READY = 0;      // Schedule Callback.streamReady()
    int NOTIFY_USER_HEADERS_RECEIVED = 1;  // Schedule/Execute Callback.onResponseHeadersReceived()
    int NOTIFY_USER_WRITE_COMPLETED = 2;   // Execute Callback.onWriteCompleted()
    int NOTIFY_USER_READ_COMPLETED = 3;    // Execute Callback.onReadeCompleted()
    int NOTIFY_USER_TRAILERS_RECEIVED = 4; // Schedule Callback.onResponseTrailersReceived()
    int NOTIFY_USER_SUCCEEDED = 5;         // Schedule/Execute Callback.onSucceeded()
    int NOTIFY_USER_NETWORK_ERROR = 6;     // Schedule Callback.onFailed()
    int NOTIFY_USER_FAILED = 7;            // Schedule Callback.onFailed()
    int NOTIFY_USER_CANCELED = 8;          // Schedule Callback.onCanceled()
    int WRITE = 9;                         // Add one more ByteBuffer to the pending queue.
    int CHAIN_NEXT_WRITE = 10;             // Initiate write completion and start next write.
    int FLUSH_HEADERS = 11;                // Start sending request headers.
    int SEND_DATA = 12;                    // Send one ByteBuffer on the wire, if any.
    int READ = 13;                         // Start reading the next chunk of the response body.
    int POSTPONE_READ = 14;                // Don't read for the moment - that action is postpone.
    int INVOKE_ON_READ_COMPLETED = 15;     // Initiate the completion of a read operation.
    int CANCEL = 16;               // Tell EM to cancel. Can be an user induced, or due to error.
    int CARRY_ON = 17;             // Do nothing special at the moment - keep calm and carry on.
    int TAKE_NO_MORE_ACTIONS = 18; // The stream is already in final state - don't do anything else.
  }

  /**
   * Bitmap used to express the global state of the BIDI Stream. Each bit represent one element of
   * the global state.
   *
   * <p>For debugging, the bits were groups by HEX digits. This "println" is very helpful - to be
   * put just before "return nextAction;"
   *
   * <pre>{@code
     System.err.println(String.format(
       "OOOO nextAction - event:%d nextAction:%d originalState:0x%08X nextState:0x%08X Thread: %s",
       event, nextAction, originalState, nextState, Thread.currentThread().getName()));
   * }</pre>
   */
  @IntDef(flag = true, // This is not used as an Enum nor as the argument of a switch statement.
          value = {State.NOT_STARTED,
                   State.STARTED,
                   State.ON_COMPLETE_RECEIVED,
                   State.USER_CANCELLED,
                   State.WAITING_FOR_FLUSH,
                   State.HEADERS_SENT,
                   State.WRITING,
                   State.END_STREAM_WRITTEN,
                   State.WRITE_DONE,
                   State.WAITING_FOR_READ,
                   State.STREAM_READY_EXECUTED,
                   State.READ_POSTPONED,
                   State.HEADERS_RECEIVED,
                   State.READING,
                   State.END_STREAM_READ,
                   State.READ_DONE,
                   State.CANCELLING,
                   State.FAILED,
                   State.DONE,
                   State.TERMINATING_STATES})
  @Retention(RetentionPolicy.SOURCE)
  private @interface State {
    // Internal state bits: Right most digit of the HEX representation: 0x0000007
    int NOT_STARTED = 0;               // Initial state.
    int STARTED = 1;                   // Started.
    int ON_COMPLETE_RECEIVED = 1 << 1; // EM's "onComplete" callback has been invoked.
    int USER_CANCELLED = 1 << 2;       // The cancel operation was initiated by the User.

    // WRITE state bits: Second and third right most digits of the HEX representation: 0x0001F0
    int WAITING_FOR_FLUSH = 1 << 4;  // User is expected to invoke "flush" at one point.
    int HEADERS_SENT = 1 << 5;       // EM's "sendHeaders" method has been invoked.
    int WRITING = 1 << 6;            // One RequestBody's Buffer is being sent on the wire.
    int END_STREAM_WRITTEN = 1 << 7; // User can't invoke "write" anymore. Maybe never could.
    int WRITE_DONE = 1 << 8;         // User won't receive more write callbacks. Maybe never had.

    // READ state bits: Fourth and fifth right most digits of the HEX representation: 0x07F000
    int WAITING_FOR_READ = 1 << 12;      // User is expected to invoke "read" at one point.
    int STREAM_READY_EXECUTED = 1 << 13; // Callback.streamReady() was executed
    int READ_POSTPONED = 1 << 14;        // User read was requested before receiving the headers.
    int HEADERS_RECEIVED = 1 << 15;      // EM's "onHeaders" callback has been invoked.
    int READING = 1 << 16;               // One ResponseBody's Buffer is being read from the wire.
    int END_STREAM_READ = 1 << 17;       // EM will not invoke the "onData" callback anymore.
    int READ_DONE = 1 << 18;             // User won't receive more read callbacks.

    // Terminating state bits: Sixth right most digit of the HEX representation: 0x700000
    int CANCELLING = 1 << 20; // EM's "cancel" method has been invoked.
    int FAILED = 1 << 21;     // An fatal failure has been encountered.
    int DONE = 1 << 22;       // Terminal state. Can be successful or otherwise.

    int TERMINATING_STATES = CANCELLING | FAILED | DONE; // Hold your breath and count to ten.
  }

  private final AtomicInteger mState = new AtomicInteger(State.NOT_STARTED);

  /**
   * Returns true if the final state has been reached. At this point the EM Stream has been
   * destroyed.
   */
  boolean isDone() { return (mState.get() & State.DONE) != 0; }

  /**
   * Returns true if a terminating state has been reached. Terminating does not necessarily means
   * that the DONE state has been reached. When the DONE bit is not set, it means that we are not
   * ready yet to inform the user about the failure, as the EM as not yet destroyed the Stream. In
   * other words, EM has not yet invoked a terminal callback (onError, onCancel, onComplete).
   */
  boolean isTerminating() { return (mState.get() & State.TERMINATING_STATES) != 0; }

  /**
   * Returns the reason why the request finished. Can only be invoked if {@link #isDone} returns
   * true.
   *
   * @return one of {@link RequestFinishedInfo#SUCCEEDED}, {@link RequestFinishedInfo#FAILED}, or
   *     {@link RequestFinishedInfo#CANCELED}
   */
  @FinishedReason
  int getFinishedReason() {
    assert isDone();
    @State int finalState = mState.get();
    if ((finalState & State.FAILED) != 0) {
      return RequestFinishedInfo.FAILED;
    }
    if ((finalState & State.USER_CANCELLED) != 0) {
      return RequestFinishedInfo.CANCELED;
    }
    return RequestFinishedInfo.SUCCEEDED;
  }

  /**
   * Establishes what is the next action by taking in account the current global state, and the
   * provided {@link Event}. This method has one important side effect: the resulting global state
   * is saved through an Atomic operation. For few cases, this method will throw when the state is
   * not compatible with the event.
   */
  @NextAction
  int nextAction(@Event final int event) {
    // With "Compare And Swap", the contract is the mutation succeeds only if the original value
    // matches the expected one - this is atomic at the assembly language level: most CPUs have
    // dedicated mnemonics for this operation - extremely efficient. And this might look like an
    // infinite loop. It is infinite only if many Threads are eternally attempting to concurrently
    // change the value. In fact, "Compare And Swap" is pretty bad under heavy contention - in
    // that case it is probably better to go with "synchronized" blocks. In our case, there is
    // none or very little contention. What matters is correctness and efficiency.
    while (true) {
      @State final int originalState = mState.get();

      if (isAlreadyFinalState(event, originalState)) {
        return NextAction.TAKE_NO_MORE_ACTIONS; // No need to loop - this is irreversible.
      }

      @NextAction final int nextAction;
      @State int nextState = originalState;
      switch (event) {
      case Event.USER_START:
      case Event.USER_START_WITH_HEADERS:
      case Event.USER_START_READ_ONLY:
      case Event.USER_START_WITH_HEADERS_READ_ONLY:
        nextState |= State.WAITING_FOR_READ | State.STARTED;
        if (event == Event.USER_START_READ_ONLY ||
            event == Event.USER_START_WITH_HEADERS_READ_ONLY) {
          nextState |= State.END_STREAM_WRITTEN | State.WRITE_DONE;
        }
        if (event != Event.USER_START_WITH_HEADERS_READ_ONLY) {
          nextState |= State.WAITING_FOR_FLUSH;
        }
        if (event == Event.USER_START_WITH_HEADERS ||
            event == Event.USER_START_WITH_HEADERS_READ_ONLY) {
          nextState |= State.HEADERS_SENT;
        }
        nextAction = NextAction.NOTIFY_USER_STREAM_READY;
        break;

      case Event.USER_LAST_WRITE:
        nextState |= State.END_STREAM_WRITTEN;
        // FOLLOW THROUGH
      case Event.USER_WRITE:
        // Note: it is fine to write even before "start" - Cronet behaves the same.
        nextAction = NextAction.WRITE;
        break;

      case Event.USER_FLUSH:
        if ((originalState & State.WAITING_FOR_FLUSH) != 0 &&
            (originalState & State.HEADERS_SENT) == 0) {
          if ((originalState & State.WRITE_DONE) != 0) {
            nextState &= ~State.WAITING_FOR_FLUSH;
          }
          nextState |= State.HEADERS_SENT;
          nextAction = NextAction.FLUSH_HEADERS;
        } else {
          nextAction = NextAction.CARRY_ON;
        }
        break;

      case Event.USER_READ:
        nextState &= ~State.WAITING_FOR_READ;
        if ((originalState & State.HEADERS_RECEIVED) == 0) {
          nextState |= State.READ_POSTPONED;
          nextAction = NextAction.POSTPONE_READ;
          // Event.READY_TO_START_POSTPONED_READ_IF_ANY will later on honor this user "read".
        } else {
          nextState |= State.READING;
          nextAction = (originalState & State.END_STREAM_READ) == 0
                           ? NextAction.READ
                           : NextAction.INVOKE_ON_READ_COMPLETED;
        }
        break;

      case Event.USER_CANCEL:
        if ((originalState & State.STARTED) == 0) {
          nextAction = NextAction.CARRY_ON; // Cancel came too soon - no effect.
        } else if ((originalState & State.ON_COMPLETE_RECEIVED) != 0) {
          nextState |= State.USER_CANCELLED | State.DONE;
          nextAction = NextAction.NOTIFY_USER_CANCELED;
        } else {
          // Due to race condition, the final EM callback can either be onCancel or onComplete.
          nextState |= State.USER_CANCELLED | State.CANCELLING;
          nextAction = NextAction.CANCEL;
        }
        break;

      case Event.ERROR:
        if ((originalState & State.ON_COMPLETE_RECEIVED) != 0 ||
            (originalState & State.STARTED) == 0) {
          nextState |= State.FAILED | State.DONE;
          nextAction = NextAction.NOTIFY_USER_FAILED;
        } else {
          // FYI: due to race condition, the final EM callback can either be onCancel or onComplete.
          nextState |= State.FAILED | State.CANCELLING;
          nextAction = NextAction.CANCEL;
        }
        break;

      case Event.STREAM_READY_CALLBACK_DONE:
        nextState |= State.STREAM_READY_EXECUTED;
        nextAction = (originalState & State.HEADERS_RECEIVED) != 0
                         ? NextAction.NOTIFY_USER_HEADERS_RECEIVED
                         : NextAction.CARRY_ON;
        break;

      case Event.ON_SEND_WINDOW_AVAILABLE:
        assert (originalState & State.WRITING) != 0;
        assert (originalState & State.WAITING_FOR_FLUSH) == 0;
        nextState |= State.WAITING_FOR_FLUSH;
        nextState &= ~State.WRITING;
        // CHAIN_NEXT_WRITE means initiate the "onCompleteReceived" user callback and send the next
        // ByteBuffer held in the mFlushData queue, if not empty.
        nextAction = NextAction.CHAIN_NEXT_WRITE;
        break;

      case Event.ON_HEADERS_END_STREAM:
        assert (originalState & State.END_STREAM_READ) == 0;
        nextState |= State.END_STREAM_READ;
        // FOLLOW THROUGH
      case Event.ON_HEADERS:
        assert (originalState & State.HEADERS_RECEIVED) == 0;
        nextState |= State.HEADERS_RECEIVED;
        nextAction = (originalState & State.STREAM_READY_EXECUTED) != 0
                         ? NextAction.NOTIFY_USER_HEADERS_RECEIVED
                         : NextAction.CARRY_ON;
        break;

      case Event.ON_DATA_END_STREAM:
        assert (originalState & State.END_STREAM_READ) == 0;
        nextState |= State.END_STREAM_READ;
        // FOLLOW THROUGH
      case Event.ON_DATA:
        assert (originalState & State.WAITING_FOR_READ) == 0;
        nextAction = NextAction.INVOKE_ON_READ_COMPLETED;
        break;

      case Event.ON_TRAILERS:
        nextAction = NextAction.NOTIFY_USER_TRAILERS_RECEIVED;
        break;

      case Event.ON_COMPLETE:
        assert (originalState & State.ON_COMPLETE_RECEIVED) == 0;
        nextState |= State.ON_COMPLETE_RECEIVED;
        if ((originalState & State.CANCELLING) != 0) {
          nextState |= State.DONE;
          nextAction = (originalState & State.FAILED) != 0 ? NextAction.NOTIFY_USER_FAILED
                                                           : NextAction.NOTIFY_USER_CANCELED;
        } else if (((originalState & State.WRITE_DONE) != 0 &&
                    (originalState & State.READ_DONE) != 0)) {
          nextState |= State.DONE;
          nextAction = NextAction.NOTIFY_USER_SUCCEEDED;
        } else {
          nextAction = NextAction.CARRY_ON;
        }
        break;

      case Event.ON_CANCEL:
        nextState |= State.DONE;
        nextAction = ((originalState & State.FAILED) != 0) ? NextAction.NOTIFY_USER_FAILED
                                                           : NextAction.NOTIFY_USER_CANCELED;
        break;

      case Event.ON_ERROR:
        nextState |= State.DONE | State.FAILED;
        nextAction = ((originalState & State.FAILED) != 0) ? NextAction.NOTIFY_USER_FAILED
                                                           : NextAction.NOTIFY_USER_NETWORK_ERROR;
        break;

      case Event.LAST_WRITE_COMPLETED:
        assert (originalState & State.WRITE_DONE) == 0;
        nextState |= State.WRITE_DONE;
        // FOLLOW THROUGH
      case Event.WRITE_COMPLETED:
        nextAction = NextAction.NOTIFY_USER_WRITE_COMPLETED;
        break;

      case Event.READY_TO_FLUSH:
        if ((originalState & State.WAITING_FOR_FLUSH) == 0) {
          nextAction = NextAction.CARRY_ON;
        } else {
          nextState &= ~State.WAITING_FOR_FLUSH;
          nextState |= State.WRITING;
          nextAction = NextAction.SEND_DATA;
        }
        break;

      case Event.READY_TO_FLUSH_LAST:
        if ((originalState & State.WAITING_FOR_FLUSH) == 0) {
          nextAction = NextAction.CARRY_ON;
        } else {
          nextState &= ~State.WAITING_FOR_FLUSH;
          nextAction = NextAction.SEND_DATA;
        }
        break;

      case Event.READY_TO_START_POSTPONED_READ_IF_ANY:
        assert (originalState & State.HEADERS_RECEIVED) != 0;
        if ((originalState & State.READ_POSTPONED) != 0) {
          nextState &= ~State.READ_POSTPONED;
          nextState |= State.READING;
          nextAction = (originalState & State.END_STREAM_READ) == 0
                           ? NextAction.READ
                           : NextAction.INVOKE_ON_READ_COMPLETED;
        } else {
          nextAction = NextAction.CARRY_ON;
        }
        break;

      case Event.READ_COMPLETED:
        assert (originalState & State.READING) != 0;
        nextState &= ~State.READING;
        nextState |= State.WAITING_FOR_READ;
        nextAction = NextAction.NOTIFY_USER_READ_COMPLETED;
        break;

      case Event.LAST_READ_COMPLETED:
        assert (originalState & State.READ_DONE) == 0;
        nextState &= ~State.READING;
        nextState |= State.READ_DONE;
        nextAction = NextAction.NOTIFY_USER_READ_COMPLETED;
        break;

      case Event.READY_TO_FINISH:
        if ((originalState & State.ON_COMPLETE_RECEIVED) != 0 &&
            (originalState & State.READ_DONE) != 0 && (originalState & State.WRITE_DONE) != 0) {
          nextState |= State.DONE;
          nextAction = NextAction.NOTIFY_USER_SUCCEEDED;
        } else {
          nextAction = NextAction.CARRY_ON;
        }
        break;

      default:
        throw new AssertionError("switch is exhaustive");
      }

      if (mState.compareAndSet(originalState, nextState)) {
        return nextAction;
      }
    }
  }

  /**
   * Returns true is we are already in a final state. However, if the provided "event" represents a
   * Terminal Network Event, then this method returns "false" even if the provided "state"
   * represents a terminating state: a Terminal Network Event needs to be processed to put the
   * Stream to rest.
   *
   * <p>For few cases, this method will throw when the state is not compatible with the event. This
   * mimics Cronet's behaviour: identical Exception types and error messages.
   */
  private static boolean isAlreadyFinalState(@Event int event, @State int state) {
    switch (event) {
    case Event.USER_START:
    case Event.USER_START_WITH_HEADERS:
    case Event.USER_START_READ_ONLY:
    case Event.USER_START_WITH_HEADERS_READ_ONLY:
      if ((state & (State.STARTED | State.TERMINATING_STATES)) != 0) {
        throw new IllegalStateException("Stream is already started.");
      }
      break;

    case Event.USER_LAST_WRITE:
    case Event.USER_WRITE:
      if ((state & State.END_STREAM_WRITTEN) != 0) {
        throw new IllegalArgumentException("Write after writing end of stream.");
      }
      break;

    case Event.USER_READ:
      if ((state & State.WAITING_FOR_READ) == 0) {
        throw new IllegalStateException("Unexpected read attempt.");
      }
      break;

    default:
      // For all other events, a potentially incompatible state does not trigger an Exception.
    }

    // Those 3 events are the final events from the EnvoyMobile C++ layer.
    if (event == Event.ON_CANCEL || event == Event.ON_ERROR || event == Event.ON_COMPLETE) {
      // If this assert triggers it means that the C++ EnvoyMobile contract has been breached.
      assert (state & State.DONE) == 0; // Or there is a blatant bug.
      // The above 3 Network Events are the only ones that need to be processed when the Stream is
      // in a terminating state. This is why here this returns "false" systematically.
      return false;
    }
    return (state & State.TERMINATING_STATES) != 0;
  }
}
