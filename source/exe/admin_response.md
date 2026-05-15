```mermaid
stateDiagram-v2
    direction TB

    [*] --> Uninit: Created

    Uninit --> GetChunks: nextChunk()
    Uninit --> Done: Cancel / Failure / Shutdown

    GetChunks --> GetChunks: nextChunk(): Sync, more data
    GetChunks --> Continue: nextChunk(): Async op started
    GetChunks --> Done: nextChunk(): Final sync chunk
    GetChunks --> Done: Error in nextChunk()
    GetChunks --> Done: Cancel / Failure / Shutdown

    Continue --> GetChunks: onChunk(): Async data received
    Continue --> Done: onDrained(): Async finished
    Continue --> Done: Error in callback
    Continue --> Done: Cancel / Failure / Shutdown

    state Done {
        description: Terminal State
    }
```

Explanation of Mermaid Syntax:

 * stateDiagram-v2: Declares a state diagram.
 * direction TB: Sets the direction from Top to Bottom.
 * [*]: Represents the start or end state.
   * [*] --> Uninit: Transition from start to Uninit.
 * State1 --> State2: Label: Defines a transition from State1 to State2 with a given Label.
 * The transitions for cancellation, failure, or shutdown are shown from each active state (Uninit, GetChunks, Continue) to the Done state.
 
