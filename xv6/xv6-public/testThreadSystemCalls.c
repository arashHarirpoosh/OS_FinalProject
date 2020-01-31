
#include "types.h"
#include "user.h"
#include "stat.h"

#define NUMOFTHREADS 7

void threadFunction(){
	printf(1, "threadID &d entered\n", getThreadID());
	exitThread();
}

int main ()
{
	int pid;
	int tid = 0;
	int allThreadstID[NUMOFTHREADS];
	pid = fork();
	for(int i=0;i<NUMOFTHREADS;i++){
		tid++;
		allThreadstID[i] = createThread(&threadFunction, malloc(4096));
	
	}
	if (pid < 0){
		printf(1, "fork failed\n");
		exit();
	} else if (pid == 0){
		printf(1, "child adding t shared counter\n");
		printf(1, "before join\n");
		for(int i=0;i<NUMOFTHREADS;i++){
			printf(1, "%d\n",joinThread(allThreadstID[i]));
		}
		printf(1, "after join \n");
		exit();
	} else {
		wait();
		printf(1, "after wait %d\n", tid);
	}
	exit();

	return 0;
}
