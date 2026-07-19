.PHONY: check-env deps check-sources test-host m1-build m1-smoke m2a-build m2a-smoke m2b-build m2b-smoke m2c-build m2c-smoke m2c-stress m3-build m3-smoke m3-stress m4-build m4-smoke m4-stress m5-build m5-smoke m6a-build m6a-smoke

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
	./tests/host/test_m4_tap_scripts.sh
	./tests/host/test_m4_smoke_script.sh
	./tests/host/test_m5_foundation.sh
	./tests/host/test_m6_runtime.sh
	./tests/host/test_m6_timer.sh
	./tests/host/test_m6_arp_timer.sh
	./tests/host/test_m6_loop.sh
	./tests/host/test_m5_netif.sh
	./tests/host/test_m5_virtio_netif.sh
	./tests/host/test_m5_ether.sh
	./tests/host/test_m5_arp.sh
	./tests/host/test_m5_ipv4_icmp.sh
	./tests/host/test_m5_stack.sh
	./tests/host/test_m5_stack_contracts.sh
	./tests/host/test_m5_e2e_contracts.sh
	./tests/host/test_m5_peer.sh
	./tests/host/test_m5_smoke_script.sh
	./tests/host/test_m6a_contracts.sh
	./tests/host/test_m6a_smoke_script.sh
	./tests/host/test_m6b_udp.sh
	./tests/host/test_m6b_exec.sh

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

m4-build: check-env check-sources
	./scripts/m4-build.sh

m4-smoke:
	./scripts/m4-smoke.sh

m4-stress: check-env check-sources
	./scripts/m4-stress.sh

m5-build: check-env check-sources
	./scripts/m5-build.sh

m5-smoke:
	./scripts/m5-smoke.sh

m6a-build: check-env check-sources
	./scripts/m6a-build.sh

m6a-smoke:
	./scripts/m6a-smoke.sh
