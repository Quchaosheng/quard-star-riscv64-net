# M3 VirtIO Block and FatFs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete M3 with a reusable legacy VirtIO MMIO/split-ring layer, interrupt-driven block I/O, and deterministic FatFs R0.15 read/write verification.

**Architecture:** Keep the existing legacy MMIO v1 transport and eight-entry split ring. Separate transport, queue, block, and FatFs responsibilities; move real I/O into the init task so waits run under the scheduler, and reuse the M2 wait/IPI machinery for completion.

**Tech Stack:** C, RISC-V GCC, VirtIO MMIO v1, legacy split virtqueue, FatFs R0.15, GCC sanitizers, Python 3 standard library, Bash, QEMU 8.0.2.

---

## File Map

- `kernel/include/timeros/virtio_mmio.h`, `kernel/src/virtio_mmio.c`: legacy transport registers, status negotiation, queue registration, notification, and IRQ acknowledgement.
- `kernel/include/timeros/virtqueue.h`, `kernel/src/virtqueue.c`: descriptor ownership and split-ring indexes with no block-specific state.
- `kernel/include/timeros/virtio.h`, `kernel/src/virtio_disk.c`: compatibility block API, request formatting, sleeping submission, and interrupt completion.
- `kernel/include/timeros/bio.h`, `kernel/src/bio.c`: serialize cache metadata and each buffer without holding the metadata lock across I/O.
- `kernel/include/timeros/fatfs_port.h`, `kernel/src/fatfs_port.c`: one-drive FatFs disk interface over sector I/O.
- `kernel/include/timeros/fatfs_test.h`, `kernel/src/fatfs_test.c`: deterministic in-guest format/write/read verification.
- `kernel/fs/fatfs/ffconf.h`: first-party FatFs build configuration.
- `scripts/prepare-fatfs.sh`: verify and allowlist-extract the fixed archive into `out/deps/fatfs`.
- `scripts/m3-build.sh`, `scripts/m3-smoke.sh`, `scripts/m3-stress.sh`: isolated M3 build and QEMU acceptance profiles.
- `tests/host/test_m3_contracts.sh`, `tests/host/test_virtqueue.c`, `tests/host/test_m3_virtqueue.sh`, `tests/host/test_m3_smoke_script.sh`: source, queue, and script behavior checks.

### Task 1: Lock M3 Contracts

**Files:**
- Create: `tests/host/test_m3_contracts.sh`
- Create: `tests/host/test_m3_smoke_script.sh`
- Modify: `Makefile`

- [ ] **Step 1: Write the failing source-contract test**

Follow the existing `require_text` and `require_order` helpers. Require these
concrete contracts:

```sh
require_text kernel/include/timeros/virtqueue.h \
  'struct virtqueue {' 'M3 needs a reusable split-ring object'
require_text kernel/include/timeros/virtio_mmio.h \
  'struct virtio_mmio {' 'M3 needs a reusable MMIO transport object'
require_text kernel/src/virtio_disk.c \
  'task_sleep(' 'block requests must sleep for completion'
require_absent kernel/src/virtio_disk.c \
  'while (_b->disk == 1)' 'block requests must not poll completion'
require_order kernel/src/task.c \
  'spin_unlock(&task_lock);' 'virtio_disk_smoke_test();' \
  'the init task must drop task_lock before block I/O'
require_text kernel/src/virtqueue.c \
  'if (id >= VIRTQ_NUM)' 'used descriptor IDs must be range checked'
require_text scripts/prepare-fatfs.sh \
  'out/deps/fatfs' 'FatFs must extract only into ignored build output'
require_text kernel/src/fatfs_test.c \
  'printk("QS:FATFS_OK\n");' 'FatFs needs a stable runtime marker'
require_text scripts/m3-smoke.sh \
  'QS:TEST_PASS:m3-smoke' 'M3 needs a stable pass marker'
```

Add `require_absent` alongside the current helpers and make it fail when the
forbidden text exists.

- [ ] **Step 2: Write the failing fake-QEMU smoke test**

Create a fake QEMU using the M2C script-test pattern. Its successful log must
contain all M2C markers plus:

```text
QS:VIRTQUEUE_OK
QS:BLOCK_IRQ_OK
QS:BLOCK_STRESS_OK
QS:FATFS_OK
QS:TEST_PASS:m3-smoke
```

Test three cases: complete markers with exit 0 pass, one missing M3 marker
fails, and complete markers with exit 7 fails.

- [ ] **Step 3: Register the tests and verify RED**

Add both scripts to `make test-host`, then run:

```bash
make test-host
```

Expected: existing tests pass; M3 tests fail only because M3 files and markers
do not exist.

### Task 2: Build and Host-Test the Common Virtqueue

**Files:**
- Create: `kernel/include/timeros/virtqueue.h`
- Create: `kernel/src/virtqueue.c`
- Create: `tests/host/test_virtqueue.c`
- Create: `tests/host/test_m3_virtqueue.sh`
- Modify: `kernel/Makefile`
- Modify: `Makefile`

- [ ] **Step 1: Write the failing queue behavior test**

Define the desired API in the test before production code exists:

```c
static void test_allocate_rollback_and_reclaim(void)
{
    static unsigned char pages[2 * 4096] __attribute__((aligned(4096)));
    struct virtqueue q;
    int chain[VIRTQ_NUM];
    int exhausted[VIRTQ_NUM];

    assert(virtq_init(&q, pages, VIRTQ_NUM) == 0);
    assert(virtq_free_count(&q) == VIRTQ_NUM);
    assert(virtq_alloc_chain(&q, 3, chain) == 0);
    assert(virtq_free_count(&q) == VIRTQ_NUM - 3);
    assert(virtq_alloc_chain(&q, VIRTQ_NUM, exhausted) == -1);
    assert(virtq_free_count(&q) == VIRTQ_NUM - 3);
    assert(virtq_free_chain(&q, chain[0]) == 3);
    assert(virtq_free_count(&q) == VIRTQ_NUM);
}

static void test_ring_wrap_and_invalid_used_id(void)
{
    static unsigned char pages[2 * 4096] __attribute__((aligned(4096)));
    struct virtqueue q;
    int chain[1];
    u16 head;

    assert(virtq_init(&q, pages, VIRTQ_NUM) == 0);
    for (int i = 0; i < VIRTQ_NUM * 3; i++) {
        assert(virtq_alloc_chain(&q, 1, chain) == 0);
        virtq_submit(&q, (u16)chain[0]);
        q.used->ring[q.used->idx % VIRTQ_NUM].id = (u32)chain[0];
        q.used->idx++;
        assert(virtq_pop_used(&q, &head) == 1);
        assert(head == (u16)chain[0]);
        assert(virtq_free_chain(&q, head) == 1);
    }
    q.used->ring[q.used->idx % VIRTQ_NUM].id = VIRTQ_NUM;
    q.used->idx++;
    assert(virtq_pop_used(&q, &head) == -1);
}

int main(void)
{
    test_allocate_rollback_and_reclaim();
    test_ring_wrap_and_invalid_used_id();
    return 0;
}
```

- [ ] **Step 2: Verify the queue test fails to compile**

Create `test_m3_virtqueue.sh` with:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$root/kernel/include" \
  "$root/tests/host/test_virtqueue.c" "$root/kernel/src/virtqueue.c" \
  -o "$tmp/test_virtqueue"
"$tmp/test_virtqueue"
```

Run it. Expected: FAIL because `virtqueue.h` and `virtqueue.c` do not exist.

- [ ] **Step 3: Implement the minimal queue API**

Define `VIRTQ_NUM` as 8 and move `virtq_desc`, `virtq_avail`,
`virtq_used_elem`, and `virtq_used` out of `virtio.h`. Define:

```c
struct virtqueue {
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    u8 free[VIRTQ_NUM];
    u8 active[VIRTQ_NUM];
    u16 used_idx;
    u16 free_count;
};

int virtq_init(struct virtqueue *q, void *pages, u16 num);
int virtq_alloc_chain(struct virtqueue *q, int count, int *indices);
int virtq_free_chain(struct virtqueue *q, u16 head);
void virtq_submit(struct virtqueue *q, u16 head);
int virtq_pop_used(struct virtqueue *q, u16 *head);
int virtq_free_count(const struct virtqueue *q);
```

`virtq_alloc_chain` must roll back each descriptor allocated in the current
attempt. `virtq_submit` marks only the chain head active. `virtq_pop_used`
returns `0` for no completion, `1` after clearing one valid active head, and
`-1` without indexing arrays when the device ID is out of range or inactive.
`virtq_free_chain` validates every next index and rejects loops longer than the
queue before changing ownership.

- [ ] **Step 4: Verify GREEN**

Run:

```bash
tests/host/test_m3_virtqueue.sh
make test-host
```

Expected: queue behavior test passes; M3 source/smoke contracts remain RED.

### Task 3: Extract Legacy MMIO and Adapt Block (M3A)

**Files:**
- Create: `kernel/include/timeros/virtio_mmio.h`
- Create: `kernel/src/virtio_mmio.c`
- Modify: `kernel/include/timeros/virtio.h`
- Modify: `kernel/src/virtio_disk.c`
- Modify: `kernel/Makefile`

- [ ] **Step 1: Add a failing MMIO source contract**

Require the status sequence and v1 queue fields in `test_m3_contracts.sh`:

```sh
require_order kernel/src/virtio_mmio.c \
  'VIRTIO_CONFIG_S_ACKNOWLEDGE' 'VIRTIO_CONFIG_S_DRIVER' \
  'MMIO status must acknowledge before claiming the driver'
require_text kernel/src/virtio_mmio.c \
  'VIRTIO_MMIO_QUEUE_ALIGN' 'legacy queue registration needs alignment'
require_text kernel/src/virtio_mmio.c \
  'VIRTIO_MMIO_QUEUE_PFN' 'legacy queue registration needs PFN'
require_absent kernel/src/virtio_mmio.c \
  'VIRTIO_MMIO_QUEUE_DESC_LOW' 'M3 must not mix modern queue registers'
```

Run the focused contract and confirm it fails on the missing implementation.

- [ ] **Step 2: Implement the transport boundary**

Define:

```c
struct virtio_mmio {
    u64 base;
    u32 device_id;
};

int virtio_mmio_init(struct virtio_mmio *dev, u64 base, u32 device_id,
                     u32 rejected_features);
int virtio_mmio_setup_queue(struct virtio_mmio *dev, u16 queue,
                            struct virtqueue *vq, void *pages);
void virtio_mmio_driver_ok(struct virtio_mmio *dev);
void virtio_mmio_notify(struct virtio_mmio *dev, u16 queue);
u32 virtio_mmio_ack_interrupt(struct virtio_mmio *dev);
u64 virtio_mmio_config64(struct virtio_mmio *dev, u32 offset);
```

`virtio_mmio_init` returns `-1` on identity or `FEATURES_OK` failure. Queue
setup rejects zero/short queues, initializes the split ring, writes page size,
queue size, alignment, and PFN, and returns `-1` on failure.

- [ ] **Step 3: Make the block driver consume the common layers**

Keep request-specific definitions in `virtio.h`, but include the two common
headers. Replace the embedded ring pointers/free array with:

```c
struct virtio_mmio mmio;
struct virtqueue queue;
char pages[2 * PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
```

Keep the current synchronous behavior only for this checkpoint, but route all
allocation, publication, used consumption, and reclamation through `virtq_*`.
Panic before indexing request metadata when `virtq_pop_used` returns `-1`.

- [ ] **Step 4: Build and run the existing M2C regression**

Run:

```bash
tests/host/test_m3_virtqueue.sh
tests/host/test_m1_kernel_contracts.sh
tests/host/test_m2c_contracts.sh
make m2c-build
make m2c-smoke
```

Expected: focused queue and pre-M3 tests pass and the real block readback still
produces `QS:BLOCK_OK`. The full M3 contract intentionally remains RED until
M3B and M3C exist.

- [ ] **Step 5: Commit M3A**

```bash
git add kernel tests/host Makefile
git commit -m "feat: add common legacy virtio queue layer"
git push origin codex/m3-virtio
```

### Task 4: Convert Block Completion to Interrupt-Driven Sleep (M3B)

**Files:**
- Modify: `kernel/src/virtio_disk.c`
- Modify: `kernel/src/main.c`
- Modify: `kernel/src/task.c`
- Modify: `kernel/include/timeros/selftest.h`
- Modify: `kernel/src/selftest.c`
- Modify: `tests/host/test_m3_contracts.sh`

- [ ] **Step 1: Add failing sleep and lifecycle contracts**

Require:

```sh
require_text kernel/src/virtio_disk.c \
  'struct wait_queue descriptors;' 'descriptor exhaustion needs a wait channel'
require_text kernel/src/virtio_disk.c \
  'struct wait_queue completion;' 'each request needs a completion channel'
require_text kernel/src/virtio_disk.c \
  'task_wake(&waiter->completion, 1);' 'IRQ completion must wake its requester'
require_absent kernel/src/main.c \
  'virtio_disk_smoke_test();' 'boot must not perform sleeping block I/O'
require_text kernel/src/task.c \
  'if (current_proc()->pid == 0)' 'only init may run the pre-user block smoke'
```

Run the focused contract. Expected: FAIL on the polling implementation.

- [ ] **Step 2: Introduce stable per-call completion state**

Use one waiter on the sleeping task's kernel stack and keep DMA header/status
storage indexed by descriptor head:

```c
struct block_waiter {
    struct wait_queue completion;
    int done;
    int result;
};

struct block_info {
    struct virtio_blk_req request;
    u8 status;
    struct block_waiter *waiter;
};
```

The ISR copies status into `waiter->result`, sets `done`, clears the info
pointer, reclaims the descriptor chain, and only then wakes completion and
descriptor waiters. This prevents a reused descriptor slot from overwriting a
sleeping caller's result.

Add `disk.failed`. Any invalid or duplicate used head or corrupted chain sets
it, prints one `QS:TEST_FAIL:m3-block:<code>` marker, fails and wakes every
active waiter, and rejects all later submissions. Initialization failures may
still panic after printing a stable code because block storage is required.

- [ ] **Step 3: Replace polling with atomic sleep/wakeup**

Initialize one block spinlock and descriptor wait queue. In the transfer path:

```c
spin_lock(&disk.lock);
while (virtq_alloc_chain(&disk.queue, 3, idx) < 0)
    task_sleep(&disk.descriptors, &disk.lock, WAIT_FOREVER);
/* initialize descriptors and submit */
while (!waiter.done)
    task_sleep(&waiter.completion, &disk.lock, WAIT_FOREVER);
spin_unlock(&disk.lock);
return waiter.result;
```

The interrupt handler acknowledges MMIO first, takes the block lock, drains
every used entry, validates active ownership/status, completes requests, and
releases the lock. It prints `QS:BLOCK_IRQ_OK` only after the first real IRQ
completion and marks the M3 self-test bit atomically.

Add these exact coordinator calls to `selftest.h` and `selftest.c`; they are
no-ops outside `QS_M3_TEST`:

```c
void m3_mark_virtqueue(void);
void m3_mark_block_irq(void);
void m3_mark_block_stress(void);
void m3_mark_fatfs(void);
```

- [ ] **Step 4: Move real I/O into init's first scheduled run**

Keep `virtio_disk_init()` in `os_main`, remove the boot-time smoke call, and
change `task_first_run` to:

```c
static void task_first_run(void)
{
    spin_unlock(&task_lock);
    if (current_proc()->pid == 0)
        virtio_disk_smoke_test();
    trap_return();
}
```

The smoke repeats deterministic sector write/read operations, verifies queue
free count and zero pending requests, prints `QS:VIRTQUEUE_OK`,
`QS:BLOCK_STRESS_OK`, and retains `QS:BLOCK_OK` for old scripts.

- [ ] **Step 5: Verify M1 and M2C on real QEMU**

Run:

```bash
make m1-build && make m1-smoke
make m2c-build && make m2c-smoke
```

Expected: both pass; serial logs contain `QS:BLOCK_IRQ_OK` and no polling path
is present.

- [ ] **Step 6: Commit M3B**

```bash
git add kernel tests/host
git commit -m "feat: make virtio block io interrupt driven"
git push origin codex/m3-virtio
```

### Task 5: Make the Existing Buffer Cache SMP-Safe

**Files:**
- Modify: `kernel/include/timeros/bio.h`
- Modify: `kernel/src/bio.c`
- Modify: `kernel/src/main.c`
- Modify: `tests/host/test_m3_contracts.sh`

- [ ] **Step 1: Add a failing cache-lock contract**

Require `struct sleeplock lock` in `bcache`, `struct sleeplock data_lock` in
each `struct buf`, both initializations in `binit`, and balanced metadata/data
lock calls in `bget`, `brelse`, `bpin`, and `bunpin`. Require `binit()` to occur
before `virtio_disk_init()` in `os_main`.

- [ ] **Step 2: Verify RED**

Run `tests/host/test_m3_contracts.sh`. Expected: FAIL because cache metadata is
currently unlocked and `binit` is not called.

- [ ] **Step 3: Add the minimal cache lock**

Serialize lookup, LRU selection, reference changes, and list movement with one
metadata sleeping lock. `bget` takes a positive reference, releases metadata,
then acquires that buffer's data lock before returning. `bread` and `bwrite`
perform device I/O while holding only the buffer data lock. `brelse` releases
the buffer data lock, then updates metadata/LRU under the metadata lock.
`bpin` and `bunpin` use only the metadata lock.

- [ ] **Step 4: Verify GREEN**

Run `tests/host/test_m3_virtqueue.sh && tests/host/test_m2c_contracts.sh`, then
`make m2c-build && make m2c-smoke`. Expected: all focused checks pass.

### Task 6: Prepare the Pinned FatFs Build Input

**Files:**
- Create: `scripts/prepare-fatfs.sh`
- Create: `kernel/fs/fatfs/ffconf.h`
- Create: `tests/host/test_m3_fatfs_prepare.sh`
- Modify: `kernel/Makefile`
- Modify: `scripts/m1-build.sh`
- Modify: `Makefile`

- [ ] **Step 1: Write the failing extraction test**

Build a temporary project containing copies of `fetch-fatfs.sh`,
`prepare-fatfs.sh`, and the first-party config. Create a ZIP containing the
expected upstream allowlist and an unrelated file. Use a temporary lock with
its real SHA-256, run `prepare-fatfs.sh`, and assert that only these files
exist below `out/deps/fatfs`:

```text
source/ff.c
source/ff.h
source/diskio.h
source/ffconf.h
LICENSE.txt
```

Also corrupt the archive and require the script to fail before extraction.

- [ ] **Step 2: Verify RED**

Run `tests/host/test_m3_fatfs_prepare.sh`. Expected: FAIL because the prepare
script does not exist.

- [ ] **Step 3: Implement allowlist extraction with Python stdlib**

The shell script runs `fetch-fatfs.sh` and then `fetch-fatfs.sh --check`, clears
only `$root/out/deps/fatfs`, and invokes `python3` with `zipfile.ZipFile`.
Extract `source/ff.c`, `source/ff.h`, `source/diskio.h`, and `LICENSE.txt`;
reject missing members and never call `extractall`. Copy first-party
`kernel/fs/fatfs/ffconf.h` into the extracted source directory last.

- [ ] **Step 4: Add the minimal FatFs configuration**

Copy R0.15's complete `source/ffconf.h` template into the first-party config,
retain every required definition, and change these values:

```c
#define FF_FS_READONLY 0
#define FF_USE_MKFS 1
#define FF_USE_LFN 0
#define FF_CODE_PAGE 437
#define FF_VOLUMES 1
#define FF_MIN_SS 512
#define FF_MAX_SS 512
#define FF_FS_EXFAT 0
#define FF_LBA64 0
#define FF_FS_NORTC 1
#define FF_FS_REENTRANT 0
```

Keep all unused optional APIs disabled. Retain the required R0.15 revision
identifier and configuration guard so `ff.h` accepts the file.

- [ ] **Step 5: Gate FatFs compilation to M3**

Add `FATFS ?= 0` and `FATFS_DIR ?= ../out/deps/fatfs/source` to the kernel
Makefile. When `FATFS=1`, add the FatFs include directory, compile `ff.c` as
`fatfs_ff.o`, and include `fatfs_port.c` and `fatfs_test.c`. Extend
`m1-build.sh` to pass `QS_KERNEL_FATFS` without changing its empty default.

- [ ] **Step 6: Verify GREEN**

Run:

```bash
tests/host/test_m3_fatfs_prepare.sh
scripts/prepare-fatfs.sh
make m1-build
```

Expected: extraction passes, M1 still builds without FatFs, and no extracted
upstream file appears in `git status`.

### Task 7: Add the FatFs Disk Port and Guest Test (M3C)

**Files:**
- Create: `kernel/include/timeros/fatfs_port.h`
- Create: `kernel/include/timeros/fatfs_test.h`
- Create: `kernel/src/fatfs_port.c`
- Create: `kernel/src/fatfs_test.c`
- Modify: `kernel/include/timeros/virtio.h`
- Modify: `kernel/src/virtio_disk.c`
- Modify: `kernel/src/task.c`
- Modify: `kernel/include/timeros/selftest.h`
- Modify: `kernel/src/selftest.c`

- [ ] **Step 1: Add failing disk-port and marker contracts**

Require all five FatFs disk functions, one logical drive check, 512-byte
sector size, `GET_SECTOR_COUNT`, `CTRL_SYNC`, `f_mkfs`, `f_mount`, `f_write`,
`f_read`, content comparison, `QS:FATFS_OK`, and M3 self-test bits.

- [ ] **Step 2: Add the sector-oriented block API**

Expose:

```c
int virtio_blk_transfer(void *data, u64 sector, u32 count, int write);
u64 virtio_blk_sector_count(void);
int virtio_blk_free_descriptors(void);
int virtio_blk_pending_requests(void);
```

Reject null data, zero count, byte lengths above `UINT32_MAX`, and ranges beyond
the device capacity. Check bounds as `sector >= capacity || count > capacity -
sector` to avoid overflow. Read the 64-bit capacity from the block config area.
Keep `virtio_disk_rw` as a wrapper using `blockno * 2` and two sectors.

- [ ] **Step 3: Implement the one-drive disk port**

`disk_initialize(0)` and `disk_status(0)` return ready; any other drive returns
not-ready/parameter error. `disk_read` and `disk_write` call the sector API and
translate nonzero results to `RES_ERROR`. `disk_ioctl` supports:

```c
CTRL_SYNC        -> RES_OK
GET_SECTOR_COUNT -> virtio_blk_sector_count()
GET_SECTOR_SIZE  -> 512
GET_BLOCK_SIZE   -> 1
```

Unknown commands return `RES_PARERR`.

- [ ] **Step 4: Implement deterministic FatFs verification**

Use static `FATFS`, `FIL`, 4096-byte mkfs work buffer, and 4096-byte read/write
buffers. Attempt mount; on `FR_NO_FILESYSTEM`, call `f_mkfs("0:", &opt, work,
sizeof(work))` and mount again. For `QS_FATFS_ITERATIONS` iterations, fill the
write buffer with `byte = iteration ^ offset ^ 0x5a`, write `/m3.bin`, sync,
close, reopen, read exactly 4096 bytes, and compare every byte.

Return the first `FRESULT`/verification error to the init hook. Print
`QS:FATFS_OK` and mark completion only after all iterations and descriptor
baseline checks pass.

Also print `QS:FATFS_ITERATIONS:%d` with the completed count and call
`m3_mark_fatfs()`, so the stress script proves the configured workload ran
instead of trusting build flags alone.

- [ ] **Step 5: Extend the self-test coordinator**

Under `QS_M3_TEST`, require M2C's allocator, wait, IPI, RFENCE, and scheduler
bits plus virtqueue, block IRQ/stress, and FatFs bits. Print
`QS:TEST_PASS:m3-smoke` (or `m3-stress`) and write `QEMU_TEST_PASS` only when
all are complete. On a FatFs test error, print
`QS:TEST_FAIL:m3-fatfs:<code>` and write `QEMU_TEST_FAIL` without panicking.

- [ ] **Step 6: Run FatFs from init before user mode**

After the init-only block smoke in `task_first_run`, call `fatfs_test_run()`
under `QS_M3_TEST`. Keep all non-M3 tasks and earlier builds on the existing
path to `trap_return`.

- [ ] **Step 7: Build the M3 kernel**

Run:

```bash
scripts/prepare-fatfs.sh
QS_STAGE=m3 \
QS_SBI_DTS="$PWD/platform/quard-star/dts/quard_star_sbi_m2.dts" \
QS_KERNEL_DTS="$PWD/platform/quard-star/dts/quard_star_kernel_m2.dts" \
QS_KERNEL_CPPFLAGS='-DQS_M2C_TEST -DQS_M3_TEST -DQS_FATFS_ITERATIONS=4' \
QS_KERNEL_FATFS=1 scripts/m1-build.sh
```

Expected: FatFs and the first-party port compile without modifying extracted
upstream files.

### Task 8: Add M3 Build, Smoke, and Stress Profiles

**Files:**
- Create: `scripts/m3-build.sh`
- Create: `scripts/m3-smoke.sh`
- Create: `scripts/m3-stress.sh`
- Modify: `scripts/m2c-smoke.sh`
- Modify: `Makefile`
- Modify: `tests/host/test_m3_smoke_script.sh`

- [ ] **Step 1: Generalize marker checking without changing M2C defaults**

Add `QS_EXTRA_MARKERS` to `m2c-smoke.sh`. After checking its existing markers,
iterate over the whitespace-separated extra list and require each marker.
When reporting failure, print each missing extra marker. Empty defaults must
preserve all M2C fake-QEMU tests. Change the stress-counter branch from an
exact `m2c-stress` comparison to `case "$test_name" in *-stress)` so both M2C
and M3 stress require allocator count, migration count, and minimum ticks.

- [ ] **Step 2: Add the quick build profile**

`m3-build.sh` runs `prepare-fatfs.sh`, selects the M2 DTS files, sets stage
`m3`, enables `QS_KERNEL_FATFS=1`, and uses:

```text
-DQS_M2C_TEST -DQS_M3_TEST -DQS_ALLOC_ITERATIONS=10000
-DQS_MIGRATION_TARGET=100 -DQS_FATFS_ITERATIONS=4
```

Then it reuses `m1-build.sh`.

- [ ] **Step 3: Add the quick smoke profile**

`m3-smoke.sh` exports stage/test name and:

```sh
QS_EXTRA_MARKERS='QS:VIRTQUEUE_OK QS:BLOCK_IRQ_OK QS:BLOCK_STRESS_OK QS:FATFS_OK' \
QS_TEST_NAME=m3-smoke exec "$root/scripts/m2c-smoke.sh"
```

- [ ] **Step 4: Add the stress profile**

`m3-stress.sh` uses an isolated `out/m3-stress`, enables the M2C stress flags,
sets `QS_FATFS_ITERATIONS=128`, and requires at least 120 seconds through
`QS_STRESS_MIN_TICKS=1200000000ULL`. It invokes the generalized smoke script
with test name `m3-stress`, the four M3 markers, and
`QS:FATFS_ITERATIONS:128`.

- [ ] **Step 5: Verify script RED/GREEN behavior**

Run:

```bash
make test-host
```

Expected: fake QEMU accepts only complete M3 markers plus exit 0; all older
script tests remain green.

- [ ] **Step 6: Commit M3C**

```bash
git add Makefile kernel scripts tests/host
git commit -m "feat: add fatfs over interrupt driven virtio block"
git push origin codex/m3-virtio
```

### Task 9: Verify and Publish M3

- [ ] **Step 1: Run host checks**

```bash
make check-env check-sources test-host
tests/host/test_m1_build_contracts.sh
```

Expected: every host test passes, including sanitizer-backed virtqueue tests.

- [ ] **Step 2: Run all earlier-stage regressions**

```bash
make m1-build && make m1-smoke
make m2a-build && make m2a-smoke
make m2b-build && make m2b-smoke
make m2c-build && make m2c-smoke
```

Expected: every build and QEMU smoke exits successfully.

- [ ] **Step 3: Run M3 quick acceptance**

```bash
make m3-build
make m3-smoke
```

Require all four M3 markers, the M2C markers, `QS:TEST_PASS:m3-smoke`, no
failure marker, zero descriptor/request leakage, and QEMU exit 0.

- [ ] **Step 4: Run the 120-second M3 stress target**

```bash
make m3-stress
```

Require 100000 allocator operations, 10000 migrations, 128 deterministic
FatFs write/read cycles, at least 1200000000 elapsed ticks, all descriptor and
request baselines restored, no failure marker, and QEMU exit 0.

- [ ] **Step 5: Inspect and publish the final state**

```bash
git diff --check
git status --short --branch
git log --oneline origin/codex/m3-virtio..HEAD
git push origin codex/m3-virtio
git fetch origin codex/m3-virtio
test "$(git rev-parse HEAD)" = "$(git rev-parse origin/codex/m3-virtio)"
```

Expected: clean worktree and identical local/remote HEAD.
