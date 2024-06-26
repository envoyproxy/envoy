package org.chromium.net.impl;

import static com.google.common.truth.Truth.assertThat;
import static org.junit.Assert.assertThrows;

import org.chromium.net.impl.CronvoyBidirectionalState.NextAction;
import org.chromium.net.impl.CronvoyBidirectionalState.Event;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

/**
 * These tests have little intrinsic value in regards with code maintenance and fighting regression
 * bugs. BidirectionalStreamTest is what matters most. Still, these constitute a form of
 * documentation, hopefully useful enough.
 *
 * <p>The Event sequence in each of these tests is deemed a plausible one. In some cases, a given
 * Event might not be strictly necessary to make the tests pass, but would be realistic.
 */
@RunWith(RobolectricTestRunner.class)
public class CronetBidirectionalStateTest {

  private final CronvoyBidirectionalState mCronetBidirectionalState =
      new CronvoyBidirectionalState();

  // ================= USER_START.* =================

  @Test
  public void userStart() {
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_START))
        .isEqualTo(NextAction.NOTIFY_USER_STREAM_READY);
  }

  @Test
  public void userStartWithHeaders() {
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS))
        .isEqualTo(NextAction.NOTIFY_USER_STREAM_READY);
  }

  @Test
  public void userStartReadOnly() {
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_START_READ_ONLY))
        .isEqualTo(NextAction.NOTIFY_USER_STREAM_READY);
  }

  @Test
  public void userStartWithHeadersReadOnly() {
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY))
        .isEqualTo(NextAction.NOTIFY_USER_STREAM_READY);
  }

  @Test
  public void userStart_twice() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    IllegalStateException exception = assertThrows(
        IllegalStateException.class, () -> mCronetBidirectionalState.nextAction(Event.USER_START));
    assertThat(exception).hasMessageThat().contains("already started");
  }

  // ================= STREAM_READY_CALLBACK_DONE =================

  @Test
  public void streamReadyCallbackDone() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    assertThat(mCronetBidirectionalState.nextAction(Event.STREAM_READY_CALLBACK_DONE))
        .isEqualTo(NextAction.CARRY_ON);
  }

  @Test
  public void streamReadyCallbackDone_afterOnHeaders() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS);
    assertThat(mCronetBidirectionalState.nextAction(Event.STREAM_READY_CALLBACK_DONE))
        .isEqualTo(NextAction.NOTIFY_USER_HEADERS_RECEIVED);
  }

  @Test
  public void streamReadyCallbackDone_afterOnHeaderEndStream() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    assertThat(mCronetBidirectionalState.nextAction(Event.STREAM_READY_CALLBACK_DONE))
        .isEqualTo(NextAction.NOTIFY_USER_HEADERS_RECEIVED);
  }

  // ================= USER_WRITE =================

  @Test
  public void userWrite() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_WRITE)).isEqualTo(NextAction.WRITE);
  }

  @Test
  public void userWrite_beforeStart() {
    // Cronet accepts that too...
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_WRITE)).isEqualTo(NextAction.WRITE);
  }

  @Test
  public void userWrite_afterStartReadOnly() {
    mCronetBidirectionalState.nextAction(Event.USER_START_READ_ONLY);
    IllegalArgumentException exception =
        assertThrows(IllegalArgumentException.class,
                     () -> mCronetBidirectionalState.nextAction(Event.USER_WRITE));
    assertThat(exception).hasMessageThat().contains("Write after writing end of stream");
  }

  @Test
  public void userWrite_afterStartWithHeadersReadOnly() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    IllegalArgumentException exception =
        assertThrows(IllegalArgumentException.class,
                     () -> mCronetBidirectionalState.nextAction(Event.USER_WRITE));
    assertThat(exception).hasMessageThat().contains("Write after writing end of stream");
  }

  @Test
  public void userWrite_afterLastWrite() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE);
    IllegalArgumentException exception =
        assertThrows(IllegalArgumentException.class,
                     () -> mCronetBidirectionalState.nextAction(Event.USER_WRITE));
    assertThat(exception).hasMessageThat().contains("Write after writing end of stream");
  }

  @Test
  public void userWrite_afterStreamDone() {
    mCronetBidirectionalState.nextAction(Event.ERROR);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_WRITE))
        .isEqualTo(NextAction.TAKE_NO_MORE_ACTIONS);
  }

  @Test
  public void userWrite_completeCycle() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS);
    mCronetBidirectionalState.nextAction(Event.USER_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH);
    mCronetBidirectionalState.nextAction(Event.ON_SEND_WINDOW_AVAILABLE);
    mCronetBidirectionalState.nextAction(Event.WRITE_COMPLETED);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_WRITE)).isEqualTo(NextAction.WRITE);
  }

  // ================= USER_LAST_WRITE =================

  @Test
  public void userLastWrite() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE))
        .isEqualTo(NextAction.WRITE);
  }

  @Test
  public void userLastWrite_beforeStart() {
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE))
        .isEqualTo(NextAction.WRITE);
  }

  @Test
  public void userLastWrite_afterStartReadOnly() {
    mCronetBidirectionalState.nextAction(Event.USER_START_READ_ONLY);
    IllegalArgumentException exception =
        assertThrows(IllegalArgumentException.class,
                     () -> mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE));
    assertThat(exception).hasMessageThat().contains("Write after writing end of stream");
  }

  @Test
  public void userLastWrite_afterStartWithHeadersReadOnly() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    IllegalArgumentException exception =
        assertThrows(IllegalArgumentException.class,
                     () -> mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE));
    assertThat(exception).hasMessageThat().contains("Write after writing end of stream");
  }

  @Test
  public void userLastWrite_afterStreamDone() {
    mCronetBidirectionalState.nextAction(Event.ERROR);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE))
        .isEqualTo(NextAction.TAKE_NO_MORE_ACTIONS);
  }

  @Test
  public void userLastWrite_afterLastWrite() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE);
    IllegalArgumentException exception =
        assertThrows(IllegalArgumentException.class,
                     () -> mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE));
    assertThat(exception).hasMessageThat().contains("Write after writing end of stream");
  }

  @Test
  public void userLastWrite_completeCycle() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS);
    mCronetBidirectionalState.nextAction(Event.USER_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH);
    mCronetBidirectionalState.nextAction(Event.ON_SEND_WINDOW_AVAILABLE);
    mCronetBidirectionalState.nextAction(Event.WRITE_COMPLETED);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE))
        .isEqualTo(NextAction.WRITE);
  }

  // ================= USER_FLUSH_DATA =================

  @Test
  public void userFlushData_afterStart() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_FLUSH))
        .isEqualTo(NextAction.FLUSH_HEADERS);
  }

  @Test
  public void userFlushData_afterStartReadOnly() {
    mCronetBidirectionalState.nextAction(Event.USER_START_READ_ONLY);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_FLUSH))
        .isEqualTo(NextAction.FLUSH_HEADERS);
  }

  @Test
  public void userFlushData_afterUserStartWithHeaders() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_FLUSH))
        .isEqualTo(NextAction.CARRY_ON);
  }

  @Test
  public void userFlushData_afterStartWithHeadersReadOnly() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_FLUSH))
        .isEqualTo(NextAction.CARRY_ON);
  }

  @Test
  public void userFlushData_beforeStart() {
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_FLUSH))
        .isEqualTo(NextAction.CARRY_ON);
  }

  @Test
  public void userFlushData_afterAnotherUserFlushData() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_FLUSH))
        .isEqualTo(NextAction.CARRY_ON);
  }

  @Test
  public void userFlushData_afterDone() {
    mCronetBidirectionalState.nextAction(Event.ERROR);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_FLUSH))
        .isEqualTo(NextAction.TAKE_NO_MORE_ACTIONS);
  }

  // ================= USER_READ =================

  @Test
  public void userRead_beforeOnHeaders() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    // Response headers not received yet - the read is postponed until then.
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_READ))
        .isEqualTo(NextAction.POSTPONE_READ);
  }

  @Test
  public void userRead_beforeOnHeaders_afterAnotherRead() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    IllegalStateException exception = assertThrows(
        IllegalStateException.class, () -> mCronetBidirectionalState.nextAction(Event.USER_READ));
    assertThat(exception).hasMessageThat().contains("Unexpected read");
  }

  @Test
  public void userRead_afterOnHeaders() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_READ)).isEqualTo(NextAction.READ);
  }

  @Test
  public void userRead_afterOnHeaders_afterAnotherRead() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    IllegalStateException exception = assertThrows(
        IllegalStateException.class, () -> mCronetBidirectionalState.nextAction(Event.USER_READ));
    assertThat(exception).hasMessageThat().contains("Unexpected read");
  }

  @Test
  public void userRead_afterOnComplete() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.ON_COMPLETE);
    // The read occurred after the stream completed - must be attended immediately by simulating
    // the reception of zero bytes. Obviously, EM won't do the callback here.
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_READ))
        .isEqualTo(NextAction.INVOKE_ON_READ_COMPLETED);
  }

  @Test
  public void userRead_afterOnComplete_afterAnotherRead() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.ON_COMPLETE);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    IllegalStateException exception = assertThrows(
        IllegalStateException.class, () -> mCronetBidirectionalState.nextAction(Event.USER_READ));
    assertThat(exception).hasMessageThat().contains("Unexpected read");
  }

  @Test
  public void userRead_beforeUserStart() {
    IllegalStateException exception = assertThrows(
        IllegalStateException.class, () -> mCronetBidirectionalState.nextAction(Event.USER_READ));
    assertThat(exception).hasMessageThat().contains("Unexpected read");
  }

  @Test
  public void userRead_completeCycle() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    mCronetBidirectionalState.nextAction(Event.ON_DATA);
    mCronetBidirectionalState.nextAction(Event.READ_COMPLETED);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_READ)).isEqualTo(NextAction.READ);
  }

  @Test
  public void userRead_afterCompletedCycle() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    mCronetBidirectionalState.nextAction(Event.ON_DATA_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.LAST_READ_COMPLETED);
    IllegalStateException exception = assertThrows(
        IllegalStateException.class, () -> mCronetBidirectionalState.nextAction(Event.USER_READ));
    assertThat(exception).hasMessageThat().contains("Unexpected read");
  }

  // ================= USER_CANCEL =================

  @Test
  public void userCancel_beforeUserStart() {
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_CANCEL))
        .isEqualTo(NextAction.CARRY_ON);
  }

  @Test
  public void cancel_beforeUserStart_afterUserLastWrite() {
    mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_CANCEL))
        .isEqualTo(NextAction.CARRY_ON);
  }

  @Test
  public void userCancel_afterUserStart() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_CANCEL))
        .isEqualTo(NextAction.CANCEL);
  }

  @Test
  public void userCancel_afterOnComplete() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.ON_COMPLETE);
    // The cancel occurred after the stream completed - Obviously, EM won't do the callback here.
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_CANCEL))
        .isEqualTo(NextAction.NOTIFY_USER_CANCELED);
  }

  @Test
  public void userCancel_afterSuccessfulReadyToFinish() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.ON_COMPLETE);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    mCronetBidirectionalState.nextAction(Event.LAST_READ_COMPLETED);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FINISH);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_CANCEL))
        .isEqualTo(NextAction.TAKE_NO_MORE_ACTIONS);
  }

  @Test
  public void userCancel_afterOnError() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.ON_ERROR);
    assertThat(mCronetBidirectionalState.nextAction(Event.USER_CANCEL))
        .isEqualTo(NextAction.TAKE_NO_MORE_ACTIONS);
  }

  // ================= ERROR =================

  @Test
  public void error_beforeUserStart() {
    // The error occurred before the stream creation - Obviously, EM won't do the callback here.
    assertThat(mCronetBidirectionalState.nextAction(Event.ERROR))
        .isEqualTo(NextAction.NOTIFY_USER_FAILED);
  }

  @Test
  public void error_beforeUserStart_afterUserLastWrite() {
    mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE);
    // The error occurred before the stream creation - Obviously, EM won't do the callback here.
    assertThat(mCronetBidirectionalState.nextAction(Event.ERROR))
        .isEqualTo(NextAction.NOTIFY_USER_FAILED);
  }

  @Test
  public void error_afterUserStart() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    // EM must be stopped first, hence the "cancel". By contract
    assertThat(mCronetBidirectionalState.nextAction(Event.ERROR)).isEqualTo(NextAction.CANCEL);
  }

  @Test
  public void error_afterOnComplete() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.ON_COMPLETE);
    // The error occurred after the stream completed - Obviously, EM won't do the callback here.
    assertThat(mCronetBidirectionalState.nextAction(Event.ERROR))
        .isEqualTo(NextAction.NOTIFY_USER_FAILED);
  }

  @Test
  public void error_afterSuccessfulReadyToFinish() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.ON_COMPLETE);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    mCronetBidirectionalState.nextAction(Event.LAST_READ_COMPLETED);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FINISH);
    assertThat(mCronetBidirectionalState.nextAction(Event.ERROR))
        .isEqualTo(NextAction.TAKE_NO_MORE_ACTIONS);
  }

  @Test
  public void error_afterAnotherError() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.ERROR);
    assertThat(mCronetBidirectionalState.nextAction(Event.ERROR))
        .isEqualTo(NextAction.TAKE_NO_MORE_ACTIONS);
  }

  @Test
  public void error_afterOnError() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.ON_ERROR);
    assertThat(mCronetBidirectionalState.nextAction(Event.ERROR))
        .isEqualTo(NextAction.TAKE_NO_MORE_ACTIONS);
  }

  // ================= READY_TO_FLUSH[_LAST] =================
  //
  // This event won't be triggered before the first USER_FLUSH. Also, it will never be triggered if
  // it is a "read only" HTTP Method (where the request body is forbidden, like GET).
  //

  @Test
  public void readyToFlush_afterUserFlush() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    assertThat(mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH))
        .isEqualTo(NextAction.SEND_DATA);
  }

  @Test
  public void readyToFlush_afterAnotherReadyToFlush() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH);            // First WRITE consumed.
    assertThat(mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH)) // Too soon - pass.
        .isEqualTo(NextAction.CARRY_ON);
  }

  @Test
  public void readyToFlush_completeCycle() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH); // Consumes first WRITE.
    mCronetBidirectionalState.nextAction(Event.ON_SEND_WINDOW_AVAILABLE);
    assertThat(mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH)) // Consumes second WRITE.
        .isEqualTo(NextAction.SEND_DATA);
  }

  @Test
  public void readyToFlushLast_afterUserFlush() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    assertThat(mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH_LAST))
        .isEqualTo(NextAction.SEND_DATA);
  }

  @Test
  public void readyToFlushLast_afterReadyToFlush() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH_LAST); // First WRITE consumed.
    assertThat(mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH_LAST)) // Too soon - pass.
        .isEqualTo(NextAction.CARRY_ON);
  }

  @Test
  public void readyToFlushLast_completeCycle() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH); // Consumes first WRITE.
    mCronetBidirectionalState.nextAction(Event.ON_SEND_WINDOW_AVAILABLE);
    assertThat(mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH_LAST)) // Last WRITE.
        .isEqualTo(NextAction.SEND_DATA);
  }

  // ================= READY_TO_START_POSTPONED_READ_IF_ANY =================
  //
  // This event won't be triggered before the ON_HEADERS[_END_STREAM] event.
  //

  @Test
  public void readyToStartPostponedReadIfAny_afterOnHeaders() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.USER_READ); // This postpones the "readData".
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS);
    assertThat(mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY))
        .isEqualTo(NextAction.READ);
  }

  @Test
  public void readyToStartPostponedReadIfAny_afterOnHeadersEndStream() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.USER_READ); // This postpones the "readData".
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    assertThat(mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY))
        .isEqualTo(NextAction.INVOKE_ON_READ_COMPLETED);
  }

  @Test
  public void readyToStartPostponedReadIfAny_afterHeaders_noPostponeRead() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    assertThat(mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY))
        .isEqualTo(NextAction.CARRY_ON);
  }

  // ================= [LAST_]WRITE_COMPLETED =================
  //
  // These events won't be triggered before the first [LAST_]FLUSH_DATA_COMPLETED.
  //

  @Test
  public void writeCompleted() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH);
    mCronetBidirectionalState.nextAction(Event.ON_SEND_WINDOW_AVAILABLE);
    assertThat(mCronetBidirectionalState.nextAction(Event.WRITE_COMPLETED))
        .isEqualTo(NextAction.NOTIFY_USER_WRITE_COMPLETED);
  }

  @Test
  public void lastWriteCompleted() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH_LAST);
    assertThat(mCronetBidirectionalState.nextAction(Event.LAST_WRITE_COMPLETED))
        .isEqualTo(NextAction.NOTIFY_USER_WRITE_COMPLETED);
  }

  // ================= [LAST_]READ_COMPLETED =================
  //
  // This event won't be triggered before the first occurrence of any of these events:
  // ON_HEADERS_END_STREAM, ON_DATA_END_STREAM, ON_DATA.
  //

  @Test
  public void readCompleted() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.ON_DATA);
    assertThat(mCronetBidirectionalState.nextAction(Event.READ_COMPLETED))
        .isEqualTo(NextAction.NOTIFY_USER_READ_COMPLETED);
  }

  @Test
  public void lastReadCompleted_afterOnHeadersEndStream() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    assertThat(mCronetBidirectionalState.nextAction(Event.LAST_READ_COMPLETED))
        .isEqualTo(NextAction.NOTIFY_USER_READ_COMPLETED);
  }

  @Test
  public void lastReadCompleted_afterOnDataEndStream() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.ON_DATA_END_STREAM);
    assertThat(mCronetBidirectionalState.nextAction(Event.LAST_READ_COMPLETED))
        .isEqualTo(NextAction.NOTIFY_USER_READ_COMPLETED);
  }

  // ================= READY_TO_FINISH =================
  //
  // This event won't be triggered before the first occurrence of any of these events: ON_COMPLETE,
  // LAST_READ_COMPLETED and LAST_WRITE_COMPLETED.
  //

  @Test
  public void readyToFinish_afterLastReadCompleted() {
    mCronetBidirectionalState.nextAction(Event.USER_START_READ_ONLY); // WRITE_DONE = true
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.ON_COMPLETE); // ON_COMPLETE_RECEIVED = true
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    mCronetBidirectionalState.nextAction(Event.LAST_READ_COMPLETED); // READ_DONE = true
    assertThat(mCronetBidirectionalState.nextAction(Event.READY_TO_FINISH))
        .isEqualTo(NextAction.NOTIFY_USER_SUCCEEDED);
  }

  @Test
  public void readyToFinish_beforeOnComplete_afterLastReadCompleted() {
    mCronetBidirectionalState.nextAction(Event.USER_START_READ_ONLY); // WRITE_DONE = true
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.LAST_READ_COMPLETED);        // READ_DONE = true
    assertThat(mCronetBidirectionalState.nextAction(Event.READY_TO_FINISH)) // Not ready yet - no-op
        .isEqualTo(NextAction.CARRY_ON);
  }

  @Test
  public void readyToFinish_afterLastWriteCompleted() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH);
    mCronetBidirectionalState.nextAction(Event.ON_SEND_WINDOW_AVAILABLE);
    mCronetBidirectionalState.nextAction(Event.WRITE_COMPLETED);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.LAST_READ_COMPLETED); // READ_DONE = true
    mCronetBidirectionalState.nextAction(Event.READY_TO_FINISH);     // Not ready yet - no-op
    mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH_LAST);
    mCronetBidirectionalState.nextAction(Event.ON_COMPLETE);          // ON_COMPLETE_RECEIVED = true
    mCronetBidirectionalState.nextAction(Event.LAST_WRITE_COMPLETED); // WRITE_DONE = true
    assertThat(mCronetBidirectionalState.nextAction(Event.READY_TO_FINISH))
        .isEqualTo(NextAction.NOTIFY_USER_SUCCEEDED);
  }

  @Test
  public void readyToFinish_beforeOnComplete_afterLastWriteCompleted() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH);
    mCronetBidirectionalState.nextAction(Event.ON_SEND_WINDOW_AVAILABLE);
    mCronetBidirectionalState.nextAction(Event.WRITE_COMPLETED);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.LAST_READ_COMPLETED); // READ_DONE = true
    mCronetBidirectionalState.nextAction(Event.READY_TO_FINISH);     // Not ready yet - no-op
    mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH_LAST);
    mCronetBidirectionalState.nextAction(Event.LAST_WRITE_COMPLETED);       // WRITE_DONE = true
    assertThat(mCronetBidirectionalState.nextAction(Event.READY_TO_FINISH)) // Not ready yet - no-op
        .isEqualTo(NextAction.CARRY_ON);
  }

  // ================= ON_SEND_WINDOW_AVAILABLE =================
  //
  // This events won't be triggered before the first READY_TO_FLUSH.
  //
  // Note: ON_SEND_WINDOW_AVAILABLE can not happen after READY_TO_FLUSH_LAST
  //

  @Test
  public void onSendWindowAvailable() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);     // Flushed Request Headers.
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH); // Flushes one non-last ByteBuffer.
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_SEND_WINDOW_AVAILABLE))
        .isEqualTo(NextAction.CHAIN_NEXT_WRITE);
  }

  // ================= ON_HEADERS[_END_STREAM] =================

  @Test
  public void onHeaders() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_HEADERS))
        .isEqualTo(NextAction.CARRY_ON);
  }

  @Test
  public void onHeadersEndStream() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM))
        .isEqualTo(NextAction.CARRY_ON);
  }

  @Test
  public void onHeader_afterStreamReadyCallbackDone() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.STREAM_READY_CALLBACK_DONE);
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_HEADERS))
        .isEqualTo(NextAction.NOTIFY_USER_HEADERS_RECEIVED);
  }

  @Test
  public void onHeaderEndSteam_afterStreamReadyCallbackDone() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.STREAM_READY_CALLBACK_DONE);
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM))
        .isEqualTo(NextAction.NOTIFY_USER_HEADERS_RECEIVED);
  }

  // ================= ON_DATA[_END_STREAM] =================

  @Test
  public void onData() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_DATA))
        .isEqualTo(NextAction.INVOKE_ON_READ_COMPLETED);
  }

  @Test
  public void onDataEndStream() {
    mCronetBidirectionalState.nextAction(Event.USER_START_WITH_HEADERS_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_DATA_END_STREAM))
        .isEqualTo(NextAction.INVOKE_ON_READ_COMPLETED);
  }

  // ================= ON_COMPLETE =================

  @Test
  public void onComplete_beforeLastWriteCompleted() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH);
    mCronetBidirectionalState.nextAction(Event.ON_SEND_WINDOW_AVAILABLE);
    mCronetBidirectionalState.nextAction(Event.WRITE_COMPLETED);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.LAST_READ_COMPLETED); // READ_DONE = true
    mCronetBidirectionalState.nextAction(Event.READY_TO_FINISH);     // Not ready yet - no-op
    mCronetBidirectionalState.nextAction(Event.USER_LAST_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH_LAST);
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_COMPLETE)) // WRITE_DONE = false
        .isEqualTo(NextAction.CARRY_ON);
  }

  @Test
  public void onComplete_beforeLastReadCompleted() {
    mCronetBidirectionalState.nextAction(Event.USER_START_READ_ONLY); // WRITE_DONE = true
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_COMPLETE)) // READ_DONE = false
        .isEqualTo(NextAction.CARRY_ON);
  }

  @Test
  public void onComplete_afterLastWriteCompleted_afterLastReadCompleted() {
    mCronetBidirectionalState.nextAction(Event.USER_START);
    mCronetBidirectionalState.nextAction(Event.USER_WRITE);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.READY_TO_FLUSH);
    mCronetBidirectionalState.nextAction(Event.ON_SEND_WINDOW_AVAILABLE);
    mCronetBidirectionalState.nextAction(Event.LAST_WRITE_COMPLETED); // WRITE_DONE = true
    mCronetBidirectionalState.nextAction(Event.READY_TO_FINISH);      // Not ready yet - no-op
    mCronetBidirectionalState.nextAction(Event.USER_READ);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
    mCronetBidirectionalState.nextAction(Event.LAST_READ_COMPLETED); // READ_DONE = true
    mCronetBidirectionalState.nextAction(Event.READY_TO_FINISH);     // Not ready yet - no-op
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_COMPLETE))
        .isEqualTo(NextAction.NOTIFY_USER_SUCCEEDED);
  }

  @Test
  public void onComplete_justAfterCancel() {
    mCronetBidirectionalState.nextAction(Event.USER_START_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.USER_CANCEL);
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_COMPLETE))
        .isEqualTo(NextAction.NOTIFY_USER_CANCELED);
  }

  @Test
  public void onComplete_justAfterError() {
    mCronetBidirectionalState.nextAction(Event.USER_START_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.USER_FLUSH);
    mCronetBidirectionalState.nextAction(Event.ON_HEADERS_END_STREAM);
    mCronetBidirectionalState.nextAction(Event.ERROR);
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_COMPLETE))
        .isEqualTo(NextAction.NOTIFY_USER_FAILED);
  }

  // ================= ON_ERROR =================

  @Test
  public void onError() {
    mCronetBidirectionalState.nextAction(Event.USER_START_READ_ONLY);
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_ERROR))
        .isEqualTo(NextAction.NOTIFY_USER_NETWORK_ERROR);
  }

  @Test
  public void onError_afterError() {
    mCronetBidirectionalState.nextAction(Event.USER_START_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.ERROR);
    // There was already a recorded error - that one has precedence.
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_ERROR))
        .isEqualTo(NextAction.NOTIFY_USER_FAILED);
  }

  // ================= ON_CANCEL =================

  @Test
  public void onCancel_afterUserCancel() {
    mCronetBidirectionalState.nextAction(Event.USER_START_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.USER_CANCEL);
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_CANCEL))
        .isEqualTo(NextAction.NOTIFY_USER_CANCELED);
  }

  @Test
  public void onCancel_afterError() {
    mCronetBidirectionalState.nextAction(Event.USER_START_READ_ONLY);
    mCronetBidirectionalState.nextAction(Event.ERROR);
    assertThat(mCronetBidirectionalState.nextAction(Event.ON_CANCEL))
        .isEqualTo(NextAction.NOTIFY_USER_FAILED);
  }
}
