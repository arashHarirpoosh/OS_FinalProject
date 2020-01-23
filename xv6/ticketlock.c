// Ticket locks

#include "types.h"
#include "defs.h"
#include "param.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"

void
initticketlock(struct ticketlock *lk, char *name)
{
    initlock(&lk->lk, "ticket lock");
    lk->name = name;
    lk->locked = 0;
    lk->pid = 0;
    lk->ticket = 0;
    lk->QHead = 0;
    lk->QTail = 0;
}

void
acquireticketlock(struct ticketlock *lk)
{
    acquire(&lk->lk);
    if (lk->locked == 0) {
        lk->waitedPid[lk->QTail] = myproc()->pid;
        lk->QTail += 1;
    }
    lk->ticket = fetch_and_add(lk->ticket, 1);
    while (lk->locked) {
        sleep(lk, &lk->lk);
    }
    lk->locked = 1;
    lk->pid = myproc()->pid;
    release(&lk->lk);
}

void
releaseticketlock(struct ticketlock *lk)
{
    acquire(&lk->lk);
    lk->locked = 0;
    lk->pid = 0;
    wakeupTicketLock(lk->waitedPid[lk->QHead]);
    lk->QHead += 1;
    release(&lk->lk);
}

int
holdingsleep(struct ticketlock *lk)
{
    int r;

    acquire(&lk->lk);
    r = lk->locked && (lk->pid == myproc()->pid);
    release(&lk->lk);
    return r;
}



