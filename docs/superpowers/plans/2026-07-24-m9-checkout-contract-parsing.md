# M9 Checkout Contract Parsing Plan

1. Add host and M8 workflow overrides for isolated contract fixtures.
2. Create downgraded-checkout fixtures containing misleading v5 comments and
   verify the current contract accepts them incorrectly.
3. Extract actual `uses:` values from each workflow and require one checkout v5.
4. Run the M9 contract, shell syntax check, and final diff validation.
