.PHONY: check-env deps check-sources test-host m1-build m1-smoke m2a-build m2a-smoke m2b-build m2b-smoke m2c-build m2c-smoke m2c-stress m3-build m3-smoke m3-stress

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
	./tests/host/test_m2b_contracts.sh
	./tests/host/test_m2b_smoke_script.sh
	./tests/host/test_m2c_contracts.sh
	./tests/host/test_m2c_smoke_script.sh
	./tests/host/test_m3_virtqueue.sh
	./tests/host/test_m3_fatfs_prepare.sh
	./tests/host/test_m3_smoke_script.sh
	./tests/host/test_m4_contracts.sh
	./tests/host/test_m4_completion.sh
	./tests/host/test_m4_smoke_script.sh

m1-build: check-env check-sources
	./scripts/m1-build.sh

m1-smoke:
	./scripts/m1-smoke.sh

m2a-build: check-env check-sources
	./scripts/m2a-build.sh

m2a-smoke:
	./scripts/m2a-smoke.sh

m2b-build: check-env check-sources
	./scripts/m2b-build.sh

m2b-smoke:
	./scripts/m2b-smoke.sh

m2c-build: check-env check-sources
	./scripts/m2c-build.sh

m2c-smoke:
	./scripts/m2c-smoke.sh

m2c-stress: check-env check-sources
	./scripts/m2c-stress.sh

m3-build: check-env check-sources
	./scripts/m3-build.sh

m3-smoke:
	./scripts/m3-smoke.sh

m3-stress: check-env check-sources
	./scripts/m3-stress.sh
