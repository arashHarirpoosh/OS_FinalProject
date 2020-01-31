//
// Created by arash on 2020-01-24.
//

#include "types.h"
#include "user.h"
#include "stat.h"

#define NUMOFTHREADS 7

void threadFunction(){
	printf(1, "Thread ID is  %d \n", getThreadID());
	sleep(100);
	exitThread();
}


int main ()
{
    int pid;
    int tid = 0;
    int allThreadstID [NUMOFTHREADS];
    for (int i=0; i< NUMOFTHREADS; i++){
        tid++;
        sleep(5);
        allThreadstID[i] = createThread(&threadFunction, malloc(4096));
    }    pid = fork();

    if (pid < 0){
        printf(1, "fork failed\n");
        exit();
    } else if (pid == 0){
        printf(1, "child adding t shared counter\n");
        printf(1, "before join\n");
        for (int i = 0; i < NUMOFTHREADS; i++) {
            sleep(70);
            printf(1,"%d\n",joinThread(allThreadstID[i]));
        }
        printf(1, "after join \n");
        exit();
    } else{
        printf(1,"before wait \n");
        wait();
        printf(1,"after wait %d\n", tid);
    }
    exit();

    return 0;
}

/*int main ()
{

    int allThreadstID [NUMOFTHREADS];
    for (int i=0; i< NUMOFTHREADS; i++){
        sleep(5);
        allThreadstID[i] = createThread(&threadFunction, malloc(4096));
    }

        printf(1, "before join\n");
        for (int i = 0; i < NUMOFTHREADS; i++) {
            sleep(70);
            printf(1,"%d\n",joinThread(allThreadstID[i]));
        }
        printf(1, "after join \n");
        exit();

    return 0;
}*/

//testThreadSystemCalls