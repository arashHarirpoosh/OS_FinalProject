//
// Created by arash on 2020-01-24.
//

#include "types.h"
#include "user.h"
#include "stat.h"

#define NCHILD 10

void threadFunction(){
	printf(1, "thread with id &d entered", getThreadID());
	exitThread();
}

int main ()
{
	struct thread allThreads[7];
	int pid = fork();
        if (pid == 0){
		for(int i=0;i < 7;i++){
			*allThreads[i] = createThread(&threadFunction, malloc(4096));
		
		}
		exit();
	}
	else if (pid > 0){
		wait();
		printf(1, "thread about to join");
		joinThread(getThreadID(allThreads[6]));
		printf(1, "thread joined");
	}
	return 0;
}
