#include <assert.h>
#include <stdint.h>

#include <timeros/net/net_sys.h>
#include <timeros/net/pktbuf.h>
#include <timeros/net/udp.h>

static void test_udp_header_validation(void)
{
    udp_hdr_t header = { 0 };

    header.src_port = 1;
    header.dest_port = 2;
    header.total_len = 8;
    assert(udp_header_check(&header, sizeof(header)) == NET_ERR_OK);
    header.total_len = 7;
    assert(udp_header_check(&header, sizeof(header)) == NET_ERR_SIZE);
    assert(udp_header_check(&header, sizeof(header) - 1) == NET_ERR_SIZE);
}

static void test_udp_bind_conflict_and_close(void)
{
    udp_pcb_t first;
    udp_pcb_t second;

    assert(net_sys_init() == NET_ERR_OK);
    assert(pktbuf_init() == NET_ERR_OK);
    assert(udp_init() == NET_ERR_OK);
    assert(udp_open(&first) == NET_ERR_OK);
    assert(udp_open(&second) == NET_ERR_OK);
    assert(udp_bind(&first, 4000) == NET_ERR_OK);
    assert(udp_bind(&second, 4000) == NET_ERR_EXIST);
    assert(udp_close(&first) == NET_ERR_OK);
    assert(udp_bind(&second, 4000) == NET_ERR_OK);
    assert(udp_close(&second) == NET_ERR_OK);
}

int main(void)
{
    test_udp_header_validation();
    test_udp_bind_conflict_and_close();
    return 0;
}
