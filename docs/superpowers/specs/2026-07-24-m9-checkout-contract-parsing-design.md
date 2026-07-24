# M9 Checkout Contract Parsing Design

## Goal

Ensure both CI workflows use exactly one active `actions/checkout@v5` step,
without allowing comments or quoted strings to satisfy the contract.

## Design

Extract action references only from executable `uses:` lines in each workflow.
Require exactly one checkout reference and require that reference to equal
`actions/checkout@v5`. Reuse the same extraction rules already applied to the
M8 cache and artifact actions.

## Alternatives

- Keep substring matching: vulnerable to comments and unrelated strings.
- Add one-off exclusions around `grep`: duplicates the parsing problem and is
  harder to audit.

## Verification

Add fixtures where the active checkout is downgraded while a v5 comment remains,
verify the old contract fails, then confirm both valid workflows pass.
