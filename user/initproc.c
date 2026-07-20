#include <timeros/types.h>
#include <timeros/syscall.h>
#include <timeros/string.h>
int main()
{
#ifdef QS_M2C_TEST
    int pid = sys_fork();
    if (pid == 0) {
        u64 start = sys_gettime();
        while (sys_gettime() - start < 100000)
            sys_yield();
        sys_exit(0);
    }
    if (pid < 0 || sys_waitpid(pid) != pid) {
        printf("QS:TEST_FAIL:m2c-wait\n");
        while (1)
            sys_yield();
    }
#endif

#if defined(QS_M6C1_TEST) || defined(QS_M6B_TEST)
    sys_exec("udp_echo");
#else
    sys_exec("user_shell");
#endif

}
