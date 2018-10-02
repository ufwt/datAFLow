# Data-flow guided fuzzing

Most popular fuzzers (e.g., AFL, libFuzzer, honggfuzz, VUzzer, etc.) are
_code-coverage_ guided. This means that they select and mutate seeds with the
aim of maximising the code coverage of the program under test (PUT). Seeds that
lead to new code being executed are therefore prioritised over other seeds.

## How does AFL record code coverage?

AFL (a popular open-source fuzzer) performs compile-time source instrumentation
to insert code that tracks _edge_ coverage (which is more accurate than _block_
coverage). This edge coverage information is stored in a bitmap (default size
64KB), where each byte in the bitmap represents the hit count of a specific
edge. The code injected at each edge is essentially equivalent to (from the
AFL documentation):

```
cur_location = <COMPILE_TIME_RANDOM>;
bitmap[cur_location ^ prev_location]++;
prev_location = cur_location >> 1;
```

Note that the last right-shift operation is performed to preserve the
directionality of edges (without this, `A -> B` would be indistinguishable from
`B -> A`).

## What issues may arise when using code coverage?

Take the following example (seems to be a classic example, used in a number of
lecture slides):

```
            1
           / \
 x = ...; 2   3
           \ /
            4
           / \
          5   6 ... = x;
           \ /
            7
```

We may execute the following two paths:

```
1 -> 2 -> 4 -> 5 -> 7
1 -> 3 -> 4 -> 6 - >7
```

Fuzzers guided by AFL may mark this code as being fully explored and
deprioritise seeds that further execute this code. However, there is a chance
we may miss any interesting behaviour that is exercised in the relationship
between the _definition_ of `x` in statement `2` and the _usage_ of `x` in
statement `6`.

## Data-flow coverage

Data-flow coverage seeks to capture _use-def_ relationships such as the one
outlined in the previous section. In "data-flow guided fuzzing", seed selection
is based on the ways in which values are associated with variables, and how
these associations can affect the execution of the program.
