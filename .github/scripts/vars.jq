[inputs | select(length>0)
        | [splits("=")]
        | {(.[0]): .[1]} ]
| add