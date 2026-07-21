#include <timeros/string.h>
#include <timeros/syscall.h>

static int fail(const char *name)
{
    printf("QS:TEST_FAIL:m7a-%s\n", name);
    return -1;
}

int main(void)
{
    uint32_t address = 0;
    if (sys_dns_resolve("m7a.test", &address) < 0)
        return fail("resolve");
    if (address != 0xc0a86401U)
        return fail("address");
    printf("QS:M7A_DNS_QUERY_OK\n");
    printf("QS:M7A_DNS_RESOLVE_OK\n");

    if (sys_dns_resolve("timeout.m7a", &address) != -4)
        return fail("timeout");
    printf("QS:M7A_DNS_TIMEOUT_OK\n");
#ifdef QS_M7B_TEST
    if (sys_exec("m7b_http_get") < 0)
        return fail("http-exec");
#else
    if (sys_dns_complete() < 0)
        return fail("complete");
#endif
    while (1)
        sys_yield();
}
