#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include<unistd.h>
#include<error.h>

int cnt=0;

void sig_timer(int signum){
	cnt++;
	printf("cnt=%d\n",cnt);
}

void sig_start(int signum){
	struct itimerval timval;
	timval.it_interval.tv_sec=1;
	timval.it_interval.tv_usec=0;
	timval.it_value.tv_sec=1;
	timval.it_value.tv_usec=0;
	printf("sig_start\n");
	if(setitimer(ITIMER_REAL,&timval,NULL)<0){
		printf("error\n");
		exit(1);
	}
}

void sig_stop(int signum){
	struct itimerval timval;
	timval.it_interval.tv_sec=0;
	timval.it_interval.tv_usec=0;
	timval.it_value.tv_sec=0;
	timval.it_value.tv_usec=0;
	printf("sig_stop\n");
	if(setitimer(ITIMER_REAL,&timval,NULL)<0){
		printf("error\n");
		exit(1);
	}
	printf("total=%d\n",cnt);
	cnt=0;
}

/*
 *  ターゲットとのシグナル通信ではターゲットのスタートボタンを押すと1秒ずつ秒数を数え始め、
 * ストップボタンを押すと秒数のカウントを止めるアプリケーションを作成した。
 * もう一度スタートボタンを押すと必ずリセットされてまた0秒からカウントを始める。
 */

int main(void){
	int fd;
	int i;
	int val_r;
	int val_w;
	int val_c;
	char buff[100];
	
	printf("Before Open\n");
	//obtain tain data from tact switch
	fd = open("/dev/tactsw",O_RDONLY); 
	printf("After Open\n");

	if(fd == -1){
		perror("ERROR : open");
		exit(1);
	}

	ioctl(fd,2,1);

		signal(SIGALRM,sig_timer);
		signal(SIGUSR1,sig_start); //start timer
		signal(SIGUSR2,sig_stop); //stop timer
	
	printf("Before Close\n");
	val_c = close(fd);
	printf("After Close\n");
	if(val_c  == -1){
		perror("ERROR : close");
		exit(1);		
	}
	for(;;){}
}



