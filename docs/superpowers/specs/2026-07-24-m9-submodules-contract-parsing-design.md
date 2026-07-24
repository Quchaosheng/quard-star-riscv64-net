# M9 Submodules Contract Parsing Design

## Goal

Make the M9 checkout contract accept valid YAML trailing comments on the
`submodules: true` setting without weakening the required boolean value.

## Design

Match the `submodules` field after removing an optional YAML comment and
trimming whitespace. Accept the YAML boolean spellings used by GitHub Actions,
while continuing to reject missing, false, or non-boolean values.

## Alternatives

- Keep exact line matching: rejects valid formatting and comments.
- Parse the whole workflow with a YAML dependency: unnecessary for one scalar
  contract and adds toolchain surface.

## Verification

Add a fixture with `submodules: true # direct checkout`, run the M9 contract,
and verify malformed values still fail. Then run the focused contract test and
shell syntax check.
