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

    sys_exec("user_shell");

}
