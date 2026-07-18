#include <timeros/os.h>

#define VIRTIO_NET_REQUIRED_FEATURES (1U << VIRTIO_NET_F_MAC)
#define VIRTIO_NET_REJECTED_FEATURES \
    ((1U << VIRTIO_NET_F_CSUM) | (1U << VIRTIO_NET_F_GUEST_CSUM) | \
     (1U << VIRTIO_NET_F_GSO) | (1U << VIRTIO_NET_F_GUEST_TSO4) | \
     (1U << VIRTIO_NET_F_GUEST_TSO6) | (1U << VIRTIO_NET_F_GUEST_ECN) | \
     (1U << VIRTIO_NET_F_GUEST_UFO) | (1U << VIRTIO_NET_F_HOST_TSO4) | \
     (1U << VIRTIO_NET_F_HOST_TSO6) | (1U << VIRTIO_NET_F_HOST_ECN) | \
     (1U << VIRTIO_NET_F_HOST_UFO) | (1U << VIRTIO_NET_F_MRG_RXBUF) | \
     (1U << VIRTIO_NET_F_STATUS) | (1U << VIRTIO_NET_F_CTRL_VQ) | \
     (1U << VIRTIO_NET_F_CTRL_RX) | (1U << VIRTIO_NET_F_CTRL_VLAN) | \
     (1U << VIRTIO_NET_F_GUEST_ANNOUNCE) | (1U << VIRTIO_NET_F_MQ) | \
     (1U << VIRTIO_NET_F_CTRL_MAC_ADDR) | (1U << VIRTIO_F_ANY_LAYOUT) | \
     (1U << VIRTIO_RING_F_EVENT_IDX) | \
     (1U << VIRTIO_RING_F_INDIRECT_DESC))

struct net_rx_slot {
    u8 bytes[VIRTIO_NET_HDR_SIZE + ETHERNET_MAX_FRAME];
    u16 descriptor;
    u8 state;
};

struct net_tx_slot {
    u8 bytes[VIRTIO_NET_HDR_SIZE + ETHERNET_MAX_FRAME];
    u16 descriptor;
    u8 in_use;
};

#define NET_RX_POSTED 1
#define NET_RX_COMPLETED 2

static struct {
    char rx_pages[2 * PAGE_SIZE];
    char tx_pages[2 * PAGE_SIZE];
    struct virtio_mmio mmio;
    struct virtqueue rx_queue;
    struct virtqueue tx_queue;
    struct spinlock lock;
    struct wait_queue rx_wait;
    struct wait_queue tx_wait;
    struct net_completion_ring rx_completions;
    struct net_rx_slot rx[VIRTQ_NUM];
    struct net_tx_slot tx[VIRTQ_NUM];
    u8 mac[6];
    u32 features;
    u32 pending_tx;
    u32 irq_reported;
    struct virtio_net_stats stats;
    int active;
    int failed;
    int resetting;
} __attribute__((aligned(PAGE_SIZE))) net;

_Static_assert(sizeof(struct virtio_net_hdr) == VIRTIO_NET_HDR_SIZE,
               "legacy virtio net header size");

static int net_post_rx_locked(int slot)
{
    int descriptor;

    if (virtq_alloc_chain(&net.rx_queue, 1, &descriptor) < 0)
        return -1;
    memset(net.rx[slot].bytes, 0, sizeof(net.rx[slot].bytes));
    net.rx[slot].descriptor = (u16)descriptor;
    net.rx[slot].state = NET_RX_POSTED;
    net.rx_queue.desc[descriptor].addr =
        (u64)(uintptr_t)net.rx[slot].bytes;
    net.rx_queue.desc[descriptor].len = sizeof(net.rx[slot].bytes);
    net.rx_queue.desc[descriptor].flags = VRING_DESC_F_WRITE;
    virtq_submit(&net.rx_queue, (u16)descriptor);
    return 0;
}

static void net_repost_rx_locked(int slot)
{
    memset(net.rx[slot].bytes, 0, VIRTIO_NET_HDR_SIZE);
    net.rx_queue.desc[net.rx[slot].descriptor].len =
        sizeof(net.rx[slot].bytes);
    net.rx[slot].state = NET_RX_POSTED;
    virtq_submit(&net.rx_queue, net.rx[slot].descriptor);
    virtio_mmio_notify(&net.mmio, VIRTIO_NET_RX_QUEUE);
}

static void net_drop_rx_locked(int slot)
{
    net.stats.rx_dropped++;
    net_repost_rx_locked(slot);
}

static int net_find_rx_slot_locked(u16 descriptor)
{
    for (int i = 0; i < VIRTQ_NUM; i++) {
        if (net.rx[i].descriptor == descriptor &&
            net.rx[i].state == NET_RX_POSTED)
            return i;
    }
    return -1;
}

static int net_validate_header_locked(int slot)
{
    for (int i = 0; i < VIRTIO_NET_HDR_SIZE; i++) {
        if (net.rx[slot].bytes[i] != 0)
            return -1;
    }
    return 0;
}

static int net_find_free_tx_slot_locked(void)
{
    for (int i = 0; i < VIRTQ_NUM; i++) {
        if (!net.tx[i].in_use)
            return i;
    }
    return -1;
}

static int net_find_tx_slot_locked(u16 descriptor)
{
    for (int i = 0; i < VIRTQ_NUM; i++) {
        if (net.tx[i].in_use && net.tx[i].descriptor == descriptor)
            return i;
    }
    return -1;
}

static void net_fail_locked(const char *code)
{
    if (!net.failed)
        printk("QS:TEST_FAIL:m4-net:%s\n", code);
    if (!net.failed)
        net.stats.tx_errors++;
    net.active = 0;
    net.failed = 1;
    task_wake(&net.rx_wait, 1);
    task_wake(&net.tx_wait, 1);
}

static int net_start_locked(void)
{
    static const u8 expected_mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };

    net_completion_init(&net.rx_completions);
    net.active = 0;
    net.pending_tx = 0;
    net.irq_reported = 0;
    for (int i = 0; i < VIRTQ_NUM; i++) {
        net.rx[i].state = 0;
        net.tx[i].in_use = 0;
    }

    if (virtio_mmio_init(&net.mmio, VIRTIO1, VIRTIO_NET_DEVICE_ID,
                         VIRTIO_NET_REJECTED_FEATURES,
                         VIRTIO_NET_REQUIRED_FEATURES, &net.features) < 0)
        goto fail;
    for (int i = 0; i < 6; i++) {
        net.mac[i] = virtio_mmio_config8(&net.mmio, (u32)i);
        if (net.mac[i] != expected_mac[i])
            goto fail;
    }
    if (virtio_mmio_setup_queue(&net.mmio, VIRTIO_NET_RX_QUEUE,
                                &net.rx_queue, net.rx_pages) < 0)
        goto fail;
    if (virtio_mmio_setup_queue(&net.mmio, VIRTIO_NET_TX_QUEUE,
                                &net.tx_queue, net.tx_pages) < 0)
        goto fail;
    for (int i = 0; i < VIRTQ_NUM; i++) {
        if (net_post_rx_locked(i) < 0)
            goto fail;
    }
    virtio_mmio_driver_ok(&net.mmio);
    virtio_mmio_notify(&net.mmio, VIRTIO_NET_RX_QUEUE);
    net.failed = 0;
    net.active = 1;
    return 0;

fail:
    (void)virtio_mmio_reset(&net.mmio);
    net.active = 0;
    net.failed = 1;
    return -1;
}

int virtio_net_init(void)
{
    spin_init(&net.lock);
    wait_queue_init(&net.rx_wait);
    wait_queue_init(&net.tx_wait);
    memset(&net.stats, 0, sizeof(net.stats));
    net.resetting = 0;
    net.failed = 1;

    spin_lock(&net.lock);
    int result = net_start_locked();
    spin_unlock(&net.lock);
    if (result < 0) {
        printk("QS:TEST_FAIL:m4-net:init\n");
        return -1;
    }
    printk("QS:NET_LINK_OK\n");
    return 0;
}

int virtio_net_get_mac(u8 *mac)
{
    if (mac == 0)
        return -1;
    spin_lock(&net.lock);
    if (!net.active || net.failed) {
        spin_unlock(&net.lock);
        return -1;
    }
    memcpy(mac, net.mac, sizeof(net.mac));
    spin_unlock(&net.lock);
    return 0;
}

int virtio_net_reset(void)
{
    spin_lock(&net.lock);
    if (net.resetting) {
        spin_unlock(&net.lock);
        return -1;
    }
    net.resetting = 1;
    net.active = 0;
    task_wake(&net.rx_wait, 1);
    task_wake(&net.tx_wait, 1);
    if (virtio_mmio_reset(&net.mmio) < 0) {
        net.failed = 1;
        net.resetting = 0;
        spin_unlock(&net.lock);
        return -1;
    }

    int result = net_start_locked();
    if (result == 0) {
        net.stats.resets++;
        net.resetting = 0;
        spin_unlock(&net.lock);
        return 0;
    }
    net.failed = 1;
    net.resetting = 0;
    spin_unlock(&net.lock);
    return -1;
}

int virtio_net_send(const void *frame, u32 length)
{
    int descriptor;
    int slot;

    if (frame == 0 || length < ETHERNET_MIN_FRAME || length > ETHERNET_MAX_FRAME)
        return -1;

    spin_lock(&net.lock);
    u64 generation = net.stats.resets;
    while (net.active && generation == net.stats.resets &&
           (slot = net_find_free_tx_slot_locked()) < 0)
        task_sleep(&net.tx_wait, &net.lock, WAIT_FOREVER);
    if (!net.active || net.failed || generation != net.stats.resets) {
        spin_unlock(&net.lock);
        return -1;
    }
    if (virtq_alloc_chain(&net.tx_queue, 1, &descriptor) < 0) {
        net_fail_locked("tx-descriptor");
        spin_unlock(&net.lock);
        return -1;
    }

    memset(net.tx[slot].bytes, 0, VIRTIO_NET_HDR_SIZE);
    memcpy(net.tx[slot].bytes + VIRTIO_NET_HDR_SIZE, frame, length);
    net.tx[slot].descriptor = (u16)descriptor;
    net.tx[slot].in_use = 1;
    net.tx_queue.desc[descriptor].addr =
        (u64)(uintptr_t)net.tx[slot].bytes;
    net.tx_queue.desc[descriptor].len = VIRTIO_NET_HDR_SIZE + length;
    net.pending_tx++;
    virtq_submit(&net.tx_queue, (u16)descriptor);
    virtio_mmio_notify(&net.mmio, VIRTIO_NET_TX_QUEUE);
    spin_unlock(&net.lock);
    return 0;
}

int virtio_net_receive(void *frame, u32 capacity, u32 *length, u64 deadline)
{
    u16 completion_slot;
    int slot;

    if (frame == 0 || length == 0)
        return -1;

    spin_lock(&net.lock);
    u64 generation = net.stats.resets;
    while (net.active && generation == net.stats.resets &&
           net.rx_completions.count == 0) {
        if (task_sleep(&net.rx_wait, &net.lock, deadline) < 0) {
            spin_unlock(&net.lock);
            return -1;
        }
    }
    if (!net.active || net.failed || generation != net.stats.resets ||
        net_completion_pop(&net.rx_completions, &completion_slot) <= 0) {
        spin_unlock(&net.lock);
        return -1;
    }
    slot = completion_slot;
    if (slot < 0 || slot >= VIRTQ_NUM ||
        net.rx[slot].state != NET_RX_COMPLETED) {
        if (net.active && !net.failed)
            net_fail_locked("rx-completion");
        spin_unlock(&net.lock);
        return -1;
    }

    u32 used_length = net.rx_queue.desc[net.rx[slot].descriptor].len;
    u32 frame_length = used_length - VIRTIO_NET_HDR_SIZE;
    if (capacity < frame_length) {
        net.stats.rx_dropped++;
        net_repost_rx_locked(slot);
        spin_unlock(&net.lock);
        return -1;
    }
    memcpy(frame, net.rx[slot].bytes + VIRTIO_NET_HDR_SIZE, frame_length);
    *length = frame_length;
    net.stats.rx_packets++;
    net_repost_rx_locked(slot);
    spin_unlock(&net.lock);
    return 0;
}

int virtio_net_free_tx_slots(void)
{
    int count = 0;

    spin_lock(&net.lock);
    for (int i = 0; i < VIRTQ_NUM; i++)
        count += !net.tx[i].in_use;
    spin_unlock(&net.lock);
    return count;
}

int virtio_net_posted_rx_buffers(void)
{
    int count = 0;

    spin_lock(&net.lock);
    for (int i = 0; i < VIRTQ_NUM; i++)
        count += net.rx[i].state == NET_RX_POSTED;
    spin_unlock(&net.lock);
    return count;
}

void virtio_net_get_stats(struct virtio_net_stats *stats)
{
    if (stats == 0)
        return;
    spin_lock(&net.lock);
    *stats = net.stats;
    spin_unlock(&net.lock);
}

int virtio_net_free_tx_descriptors(void)
{
    spin_lock(&net.lock);
    int count = virtq_free_count(&net.tx_queue);
    spin_unlock(&net.lock);
    return count;
}

int virtio_net_pending_tx(void)
{
    spin_lock(&net.lock);
    int pending = (int)net.pending_tx;
    spin_unlock(&net.lock);
    return pending;
}

int virtio_net_rx_completions(void)
{
    spin_lock(&net.lock);
    int count = (int)net.rx_completions.count;
    spin_unlock(&net.lock);
    return count;
}

#ifndef QS_NET_ITERATIONS
#define QS_NET_ITERATIONS 32
#endif
#ifndef QS_NET_RESETS
#define QS_NET_RESETS 1
#endif

#define M4_ETHERTYPE 0x88b5
#define M4_PAYLOAD_SIZE 32
#define M4_FRAME_SIZE ETHERNET_MIN_FRAME
#define M4_RX_TIMEOUT_TICKS 50000000ULL

static void net_put_be16(u8 *p, u16 value)
{
    p[0] = (u8)(value >> 8);
    p[1] = (u8)value;
}

static void net_put_be32(u8 *p, u32 value)
{
    p[0] = (u8)(value >> 24);
    p[1] = (u8)(value >> 16);
    p[2] = (u8)(value >> 8);
    p[3] = (u8)value;
}

static u16 net_get_be16(const u8 *p)
{
    return (u16)(((u16)p[0] << 8) | p[1]);
}

static u32 net_get_be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
           ((u32)p[2] << 8) | p[3];
}

static u32 net_payload_checksum(const u8 *payload, u16 length)
{
    u32 checksum = 0;
    for (u16 i = 0; i < length; i++)
        checksum += payload[i];
    return checksum;
}

static void net_build_request(u8 *frame, u32 sequence)
{
    memset(frame, 0, M4_FRAME_SIZE);
    memset(frame, 0xff, 6);
    memcpy(frame + 6, net.mac, 6);
    net_put_be16(frame + 12, M4_ETHERTYPE);
    net_put_be32(frame + 14, sequence);
    net_put_be16(frame + 18, M4_PAYLOAD_SIZE);
    for (u16 i = 0; i < M4_PAYLOAD_SIZE; i++)
        frame[20 + i] = (u8)(sequence ^ i ^ 0x5a);
    net_put_be32(frame + 20 + M4_PAYLOAD_SIZE,
                 net_payload_checksum(frame + 20, M4_PAYLOAD_SIZE));
}

static int net_validate_response(const u8 *frame, u32 length, u32 sequence)
{
    static const u8 host_mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x57 };

    if (length < ETHERNET_HEADER_SIZE || memcmp(frame, net.mac, 6) != 0 ||
        net_get_be16(frame + 12) != M4_ETHERTYPE)
        return 1;
    if (length < 24 || memcmp(frame + 6, host_mac, 6) != 0 ||
        net_get_be32(frame + 14) != sequence)
        return -1;
    u16 payload_length = net_get_be16(frame + 18);
    if (payload_length != M4_PAYLOAD_SIZE ||
        20U + payload_length + 4U > length)
        return -1;
    for (u16 i = 0; i < payload_length; i++) {
        if (frame[20 + i] != (u8)(sequence ^ i ^ 0x5a))
            return -1;
    }
    return net_get_be32(frame + 20 + payload_length) ==
           net_payload_checksum(frame + 20, payload_length) ? 0 : -1;
}

static int net_wait_tx_idle(u64 deadline)
{
    spin_lock(&net.lock);
    u64 generation = net.stats.resets;
    while (net.active && generation == net.stats.resets && net.pending_tx != 0) {
        if (task_sleep(&net.tx_wait, &net.lock, deadline) < 0) {
            spin_unlock(&net.lock);
            return -1;
        }
    }
    int result = net.active && !net.failed && generation == net.stats.resets ?
                 0 : -1;
    spin_unlock(&net.lock);
    return result;
}

int virtio_net_raw_test(void)
{
#ifdef QS_M4_TEST
    static u8 tx_frame[M4_FRAME_SIZE];
    static u8 rx_frame[ETHERNET_MAX_FRAME];
    u32 resets_done = 0;

    m4_mark_net_link();
    for (u32 sequence = 0; sequence < QS_NET_ITERATIONS; sequence++) {
        net_build_request(tx_frame, sequence);
        if (virtio_net_send(tx_frame, sizeof(tx_frame)) < 0)
            return -1;
        if (sequence == 0) {
            printk("QS:NET_TX_OK\n");
            m4_mark_net_tx();
        }

        u64 deadline = r_mtime() + M4_RX_TIMEOUT_TICKS;
        for (;;) {
            u32 received = 0;
            if (virtio_net_receive(rx_frame, sizeof(rx_frame), &received,
                                   deadline) < 0)
                return -2;
            int validation = net_validate_response(rx_frame, received,
                                                   sequence);
            if (validation == 0)
                break;
            if (validation < 0)
                return -2;
        }
        if (sequence == 0) {
            printk("QS:NET_RX_OK\n");
            m4_mark_net_rx();
        }

        if (resets_done < QS_NET_RESETS &&
            2 * (sequence + 1) * QS_NET_RESETS >=
                (2 * resets_done + 1) * QS_NET_ITERATIONS) {
            if (net_wait_tx_idle(r_mtime() + M4_RX_TIMEOUT_TICKS) < 0 ||
                virtio_net_reset() < 0)
                return -3;
            resets_done++;
            if (resets_done == 1) {
                printk("QS:NET_RESET_OK\n");
                m4_mark_net_reset();
            }
        }
    }

    if (net_wait_tx_idle(r_mtime() + M4_RX_TIMEOUT_TICKS) < 0 ||
        virtio_net_free_tx_descriptors() != VIRTQ_NUM ||
        virtio_net_pending_tx() != 0 || virtio_net_rx_completions() != 0 ||
        virtio_net_free_tx_slots() != VIRTQ_NUM ||
        virtio_net_posted_rx_buffers() != VIRTQ_NUM)
        return -4;

    struct virtio_net_stats stats;
    virtio_net_get_stats(&stats);
    if (stats.resets != QS_NET_RESETS)
        return -5;
    printk("QS:NET_RESETS:%d\n", (int)stats.resets);
    printk("QS:NET_STRESS_FRAMES:%d\n", QS_NET_ITERATIONS);
    m4_mark_net_stress();
    return 0;
#else
    return -1;
#endif
}

void virtio_net_intr(void)
{
    virtio_mmio_ack_interrupt(&net.mmio);
    spin_lock(&net.lock);
    net.stats.interrupts++;

    for (;;) {
        u32 used_length;
        u16 descriptor;
        int result = virtq_pop_used_len(&net.rx_queue, &descriptor,
                                        &used_length);
        if (result == 0)
            break;
        if (result < 0) {
            net_fail_locked("rx-used-id");
            break;
        }
        int slot = net_find_rx_slot_locked(descriptor);
        if (slot < 0) {
            net_fail_locked("rx-ownership");
            break;
        }
        if (used_length > VIRTIO_NET_HDR_SIZE + ETHERNET_MAX_FRAME) {
            net_fail_locked("rx-length");
            break;
        }
        if (net_validate_header_locked(slot) < 0 ||
            used_length < VIRTIO_NET_HDR_SIZE + ETHERNET_HEADER_SIZE) {
            net_drop_rx_locked(slot);
            continue;
        }
        net.rx_queue.desc[descriptor].len = used_length;
        net.rx[slot].state = NET_RX_COMPLETED;
        if (net_completion_push(&net.rx_completions, slot) < 0) {
            net_drop_rx_locked(slot);
        } else {
            task_wake(&net.rx_wait, 1);
        }
        if (!__atomic_exchange_n(&net.irq_reported, 1, __ATOMIC_ACQ_REL)) {
            printk("QS:NET_IRQ_OK\n");
            m4_mark_net_irq();
        }
    }

    for (;;) {
        u16 descriptor;
        int result = virtq_pop_used(&net.tx_queue, &descriptor);
        if (result == 0)
            break;
        if (result < 0) {
            net_fail_locked("tx-used-id");
            break;
        }
        int slot = net_find_tx_slot_locked(descriptor);
        if (slot < 0 || virtq_free_chain(&net.tx_queue, descriptor) != 1) {
            net_fail_locked("tx-ownership");
            break;
        }
        net.tx[slot].in_use = 0;
        net.pending_tx--;
        net.stats.tx_packets++;
        task_wake(&net.tx_wait, 1);
        if (!__atomic_exchange_n(&net.irq_reported, 1, __ATOMIC_ACQ_REL)) {
            printk("QS:NET_IRQ_OK\n");
            m4_mark_net_irq();
        }
    }
    spin_unlock(&net.lock);
}
