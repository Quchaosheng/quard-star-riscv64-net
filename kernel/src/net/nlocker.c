#include <timeros/net/nlocker.h>

void nlocker_init(nlocker_t *locker, nlocker_type_t type)
{
    locker->type = type;
    spin_init(&locker->lock);
}

void nlocker_destroy(nlocker_t *locker)
{
    (void)locker;
}

void nlocker_lock(nlocker_t *locker)
{
    if (locker->type != NLOCKER_NONE)
        spin_lock(&locker->lock);
}

void nlocker_unlock(nlocker_t *locker)
{
    if (locker->type != NLOCKER_NONE)
        spin_unlock(&locker->lock);
}
