# M9 Submodules Contract Parsing Plan

1. Add a fixture with a trailing YAML comment on `submodules: true` and run the
   contract to verify the current exact matcher fails.
2. Replace exact matching with comment-aware scalar validation.
3. Verify the commented valid value passes and false/malformed values fail.
4. Inspect the diff and commit the focused change.
