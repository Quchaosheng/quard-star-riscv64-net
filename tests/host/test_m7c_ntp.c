#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <timeros/net/net_err.h>

int ntp_request_encode(unsigned char *, int, uint32_t);
int ntp_response_parse(const unsigned char *, int, const unsigned char *,
                       uint64_t *);

int main(void)
{
    unsigned char request[48];
    unsigned char response[48];
    uint64_t seconds = 0;

    assert(ntp_request_encode(request, sizeof(request), 100) == 48);
    assert(request[0] == 0x1b);
    memset(response, 0, sizeof(response));
    response[0] = 0x24;
    response[1] = 1;
    memcpy(response + 24, request + 40, 8);
    response[40] = 0x83;
    response[41] = 0xaa;
    response[42] = 0x7e;
    response[43] = 0xfb;
    assert(ntp_response_parse(response, sizeof(response), request,
                              &seconds) == NET_ERR_OK);
    assert(seconds == 123);
    assert(ntp_response_parse(response, 47, request, &seconds) ==
           NET_ERR_SIZE);
    response[0] = 0x21;
    assert(ntp_response_parse(response, sizeof(response), request,
                              &seconds) == NET_ERR_FORMAT);
    response[0] = 0x24;
    response[1] = 0;
    assert(ntp_response_parse(response, sizeof(response), request,
                              &seconds) == NET_ERR_FORMAT);
    response[1] = 1;
    response[24] ^= 1;
    assert(ntp_response_parse(response, sizeof(response), request,
                              &seconds) == NET_ERR_FORMAT);
    return 0;
}
