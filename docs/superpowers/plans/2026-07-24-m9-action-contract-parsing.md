# M9 Action Contract Parsing Plan

1. Add M9 fixtures with comment and quoted-string action references that must
   not affect active action counts.
2. Run the contract test and verify the fixture fails under the current
   counting logic.
3. Replace global action-name counts with extraction of active `uses:` values.
4. Verify the focused M9 test and inspect the final diff.
