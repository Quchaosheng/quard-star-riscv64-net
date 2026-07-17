.PHONY: check-env deps check-sources test-host m1-build m1-smoke

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

m1-build: check-env check-sources
	./scripts/m1-build.sh

m1-smoke:
	./scripts/m1-smoke.sh
