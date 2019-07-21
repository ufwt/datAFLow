# datAFLow-guided fuzzing

This README describes the modifications made to AFL to support dataflow-guided
fuzzing.

## `dataflow-collect`

The Makefile contains an additonal target, `dataflow-collect`, for collecting
`N` inputs generated during a fuzzing campaign (where `N` can be set by the
`DATAFLOW_MAX_INPUTS` environment variable).

When collecting inputs, havoc mode is disabled so that only deterministic
mutations are performed. This makes it easier to compare/repeat across
campaigns.
