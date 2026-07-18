.PHONY: check-env deps check-sources test-host m1-build m1-smoke m2a-build m2a-smoke

check-env:
	./scripts/check-env.sh

deps:
	git submodule update --init --depth 1
	./scripts/fetch-fatfs.sh

check-sources:
	./scripts/check-sources.sh

test-host:
	./tests/host/test_m0_scripts.sh
	./tests/host/test_m1_smoke_script.sh
	./tests/host/test_m1_dts.sh
	./tests/host/test_m1_kernel_contracts.sh
	./tests/host/test_m2a_contracts.sh
	./tests/host/test_m2a_smoke_script.sh

m1-build: check-env check-sources
	./scripts/m1-build.sh

m1-smoke:
	./scripts/m1-smoke.sh

m2a-build: check-env check-sources
	./scripts/m2a-build.sh

m2a-smoke:
	./scripts/m2a-smoke.sh
