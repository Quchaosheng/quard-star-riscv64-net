# Host Test Python Cache Cleanup Plan

1. Add a failing regression check around `test_m7e_peer.sh` that removes any
   pre-existing `scripts/__pycache__`, runs the test logic, and fails if the
   directory appears.
2. Set `PYTHONDONTWRITEBYTECODE=1` on the M7E peer test invocation.
3. Run the focused test to verify red then green behavior.
4. Run `make test-host` and `make test-build`, inspect the diff, and commit.
