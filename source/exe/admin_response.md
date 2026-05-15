```mermaid
  stateDiagram-v2 direction TB [*] --> Uninit: Created Uninit --> GetChunks: nextChunk() state
  GetChunks { [*] --> Ready Ready --> Ready: nextChunk(): Sync chunk, more to come Ready -->
  WaitingAsync: nextChunk(): Async operation started Ready --> [*]: nextChunk(): Final sync chunk }
  GetChunks --> Continue: Async op started Continue --> GetChunks: onChunk(): Async data received
  Continue --> Done: onDrained(): Async finished Uninit --> Done: Cancel / Failure / Shutdown
  GetChunks --> Done: Cancel / Failure / Shutdown / Error Continue --> Done: Cancel / Failure /
  Shutdown / Error state Done <<choice>> GetChunks -down-> Done
```

Explanation of Mermaid Syntax:

 * stateDiagram-v2: Declares a state diagram.
 * direction TB: Sets the direction from Top to Bottom.
 * [*]: Represents the start or end state.
   * [*] --> Uninit: Transition from start to Uninit.
 * State1 --> State2: Label: Defines a transition from State1 to State2 with a given Label.
 * The transitions for cancellation, failure, or shutdown are shown from each active state (Uninit, GetChunks, Continue) to the Done state.
 
