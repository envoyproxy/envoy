# Compiled string map algorithm

## The trie-like structure

The data structure consists of:
1. a length branch node - strings are grouped by length and a zero-indexed length lookup table is generated. Entries in the table may be nullptr, indicating there are no strings of that length, or a pointer to another node.
2. a branch node - similar to a standard trie, a node branches by a set of characters at an index. Unlike a standard trie, the index is not the "first" index, but instead is the index at which the most branches would be generated. The node contains the index to be branched on, the lowest character value in the branches, and a vector from lowest-to-highest character value, e.g. if branches c and f exist, a vector representing [c][d][e][f] will be in the node, with the [d] and [e] branches being nullptr.
3. a leaf node. Leaf nodes contain the entire string for final validation, and the value to be returned if the search key matches the string.

## The compile step

```
 ┌───────────────────┐
 │ x-envoy-banana    │
 │ x-envoy-pineapple │
 │ x-envoy-babana    │
 │ x-envoy-grape     │
 │ x-envoy-bacana    ├──┐
 │ x-envoy-banara    │  │
 │ something-else    │  │
 └───────────────────┘  │
                        ▼
                   split by length
                        │
                        │
                        │  ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐
                        └─►│ 0│ 1│ 2│ 3│ 4│ 5│ 6│ 7│ 8│ 9│10│11│12│13│14│15│16│17│
                           └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴┬─┴┬─┴──┴──┴┬─┘
                                                                   │  │        │
      ┌────────────────────────────────────────────────────────────┘  │        │
      │                                                               │        │
      ▼                ┌──────────────────────────────────────────────┘        │
┌─────────────┐        │                                                       ▼
│x-envoy-grape│        │                                        ┌─────────────────┐
└─────────────┘        │                                        │x-envoy-pineapple│
                       ▼                                        └─────────────────┘
           ┌────────────────┐
           │ x-envoy-banana │
           │ x-envoy-babana │
           │ x-envoy-bacana │
           │ x-envoy-banara │
           │ something-else │
           └─┬──────────────┘
             │
             │ Find best branch index (maximum unique branches)
             │
             │
             ▼

            x-envoy-banana
                      b r
                      c
            something-else

            22222222224232

                      ^ best index is here with 4 branches, n b c and e
             │
             │
             │
             ▼ branch node at position 10, index 0 = b
            ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐
            │b │c │d │e │f │g │h │i │j │k │l │m │n │
            └┬─┴┬─┴──┴┬─┴──┴──┴──┴──┴──┴──┴──┴──┴┬─┘
             │  │     │                          │
             ▼  │     ▼                          ▼
x-envoy-babana  │  something-else        ┌────────────────┐
                ▼                        │ x-envoy-banana │
    x-envoy-bacana                       │ x-envoy-banara │
                                         └───────┬────────┘
                                                 │
                                                 ▼
                                         Find best index
                                         it's position 12 with 2 branches
                                                 │
  branch node at position 12, index 0 = n        ▼
                                   ┌──┬──┬──┬──┬──┐
                                   │n │o │p │q │r │
                                   └┬─┴──┴──┴──┴┬─┘
                                    │           │
                                    ▼           ▼
                       x-envoy-banana         x-envoy-banara
```

## The lookup

A lookup operation simply walks the generated tree in much the same way as a regular trie.
For example, given the tree generated above, if you were to search for the header `sponge`,
the length lookup would find nullptr at index 6, and the resulting value would be the null
value.

If you were to search for `x-envoy-banaka`, the length would find the branch with most of
the entries, the index 10 branch would take branch `n`, and the index 12 branch would see that
`k` is lower than the minimum index in the vector, so the result would be the null value.

If you were to search for `y-envoy-banara`, the length would find the branch with most of
the entries, the index 10 branch would take branch `n`, the index 12 branch would take
branch `r`, and the leaf node would do a final string compare, see that
`y-envoy-barana` != `x-envoy-banara`, and the result would be the null value.

## Performance

In most cases this will be faster than a regular trie, especially for hits - for example,
instead of 14 steps to match `x-envoy-banana` in the example above, it takes 3 and a
final memory comparison (which is much faster than one character at a time). For misses,
some cases will be faster (when the target shares a prefix with an entry), but some will
be very slightly slower due to more expensive comparisons and dynamic function selection
in the compiled version.

Benchmark versus a fixed-size 256-branch trie, for the static header map - hit benchmark
searches for the full range of static headers, miss benchmark used a small set of arbitrary
non-matching headers:

Benchmark | trie | compiled | trieStdDev | compiledStdDev | change
-- | -- | -- | -- | -- | --
bmHeaderMapImplRequestStaticLookupHits | 47.2ns | 16.4ns | 0.629 | 0.378 | -65.3%
bmHeaderMapImplResponseStaticLookupHits | 34.7ns | 14.3ns | 0.571 | 0.085 | -58.8%
bmHeaderMapImplRequestStaticLookupMisses | 6.89ns | 6.83ns | 0.044 | 0.034 | -0.01%
bmHeaderMapImplResponseStaticLookupMisses | 6.40ns | 7.31ns | 0.028 | 0.057 | +14.2%

Versus an `absl::flat_hash_map`, hits are slightly faster and misses are significantly
faster. (Benchmark has been lost to time.)

In terms of memory, unlike a regular trie, this structure must contain the full keys
in the leaf nodes, which could potentially make it larger due to, e.g. containing
`x-envoy-` multiple times where a regular trie only has one 'branch' for that.
However, it also contains fewer nodes due to not having a node for every character,
and compared to the prior fixed-size-node 256-branch trie, each node is a lot smaller;
each node in the prior trie used 8kb, versus an average node in this trie uses less
than 60 bytes.

## Limitations

This structure is only useful for non-dynamic data - the cost of the compile step
will outweigh any lookup benefits if the contents need to be modified.

Unlike a regular trie, this structure does not facilitate prefix-matching - you
can't find a "nearest prefix" or a "longest common prefix".

A `gperf` hash would likely be faster than this for hits and slightly slower for
misses, but also has the additional constraint that the contents must be known
before binary-compile-time; envoy's use-case supports extensions dynamically
adding to the contents, which therefore precludes `gperf`, or at least makes it
impractical.
