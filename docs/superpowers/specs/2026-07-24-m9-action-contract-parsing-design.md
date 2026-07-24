# M9 Action Contract Parsing Design

## Goal

Keep the M9 CI contract focused on executable GitHub Action references rather
than matching action names in YAML comments or quoted strings.

## Design

Extract action references only from uncommented `uses:` lines in the
`qemu-smoke` job. The contract must require exactly one active
`actions/cache@v6` and one active `actions/upload-artifact@v7`, while keeping
the existing disabled-step rejection.

## Alternatives

- Add exclusions to the current global `grep` expressions: fragile around YAML
  quoting and comments.
- Use a YAML parser: unnecessary dependency and more surface area for this
  narrow shell contract.

## Verification

Add regression fixtures containing comments and quoted action references, run
the M9 contract test, then run the focused host-test checks available locally.
