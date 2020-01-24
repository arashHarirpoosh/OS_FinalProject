//
// Created by arash on 2020-01-24.
//

// Ticket locks

#include "types.h"
#include "defs.h"
#include "param.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "ticketlock.h"
#include "readerwriterlock.h"

void initpatter(struct readerwriter *lk);
int findreaders(int pattern[100], int head, int tail);
initreaderwriterlock(struct readerwriterlock *lk, char *name)
{
    initlock(&lk->lk, "ticket lock");
    lk->name = name;
    lk->locked = 0;
    lk->pid = 0;
    initticketlock(&lk->mutex, "m");
    initticketlock(lk->writer);
    initpattern(lk);
}

void
acquirereaderwriterlock(struct readerwriterlock *lk, int pattern)
{
    acquireticketlock(&lk->lk);
    if (lk->locked) {
        lk->waitedPid[lk->QTail] = myproc()->pid;
        fetch_and_add(&lk->QTail, 1);
        lk->QTail %= 100;
    }
    fetch_and_add(&lk->ticket, 1);
    while (lk->locked) {
        sleep(lk, &lk->lk);
    }
    lk->locked = 1;
    lk->pid = myproc()->pid;
    releaseticketlock(&lk->lk);

}

void
releasereaderwriterlock(struct readerwriterlock *lk)
{
    acquireticketlock(&lk->lk);
    lk->locked = 0;
    lk->pid = 0;
    fetch_and_add(&lk->QHead, 1);
    if(lk->QHead == 100){
        lk->QHead = 0;
    }
    wakeupTicketLock(lk->waitedPid[lk->QHead]);
    releaseticketlock(&lk->lk);
}

/*int
holdingticket(struct ticketlock *lk)
{
    int r;

    acquire(&lk->lk);
    r = lk->locked && (lk->pid == myproc()->pid);
    release(&lk->lk);
    return r;
}
*/


