#include <time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <wiringPi.h>
#include <stdint.h>
#include <string.h>
#include <sys/sysinfo.h>
#include "PCD8544.h"
 
#define SIGTIMER     (SIGRTMAX)
 
timer_t SetTimer(int signo, int sec, int mode);
void SignalHandler(int signo, siginfo_t * info, void *context);
timer_t timerid;
void initDisplay(void);
void initTimer(void);
void drawLCD(void);

// pin setup
int _din = 1;
int _sclk = 0;
int _dc = 2;
int _rst = 4;
int _cs = 3;
  
// lcd contrast 
int contrast = 60;
int diveTime = 0;

void initDisplay(void) {
	// init and clear lcd
	LCDInit(_sclk, _din, _dc, _cs, _rst, contrast);
	LCDclear();
	LCDdisplay();
}

void initTimer(void) {
        struct sigaction sigact;
 
        sigemptyset(&sigact.sa_mask);
        sigact.sa_flags = SA_SIGINFO;
        sigact.sa_sigaction = SignalHandler;
 
        // Set up sigaction to catch signal
        if (sigaction(SIGTIMER, &sigact, NULL) == -1) {
                printf("sigaction failed\n");
                exit(1);
        }
 
        // Establish a handler to catch CTRL+C and use it for exiting
        sigaction(SIGINT, &sigact, NULL);
 
        timerid = SetTimer(SIGTIMER, 1000, 1);
}

void drawLCD(void) {
	LCDclear();

	// Display Dive Time
	char diveTimeChar[20];
	if(diveTime >= 60) {
		snprintf(diveTimeChar, 20, "Dive time:%ldm", (int)diveTime/60);
	} else {
		snprintf(diveTimeChar, 20, "Dive time:%lds", diveTime);
	}
	LCDdrawstring(0,0,diveTimeChar);
	LCDdisplay();
}

int main()
{

	// check wiringPi setup
	if (wiringPiSetup() == -1)
	{
		printf("wiringPi-Error\n");
		exit(1);
	}
  
	initDisplay();
	initTimer();

        for(;;)
           ;

        return 0;
}

timer_t SetTimer(int signo, int sec, int mode)
{
        struct sigevent sigev;
        timer_t timerid;
        struct itimerspec itval;
        struct itimerspec oitval;
 
        // Create the POSIX timer to generate signo
        sigev.sigev_notify = SIGEV_SIGNAL;
        sigev.sigev_signo = signo;
        sigev.sigev_value.sival_ptr = &timerid;
 
        if (timer_create(CLOCK_REALTIME, &sigev, &timerid) == 0) {
                itval.it_value.tv_sec = sec / 1000;
                itval.it_value.tv_nsec = (long)(sec % 1000) * (1000000L);
 
                if (mode == 1) {
                        itval.it_interval.tv_sec = itval.it_value.tv_sec;
                        itval.it_interval.tv_nsec = itval.it_value.tv_nsec;
                } else {
                        itval.it_interval.tv_sec = 0;
                        itval.it_interval.tv_nsec = 0;
                }
 
                if (timer_settime(timerid, 0, &itval, &oitval) != 0) {
                        printf("time_settime error!\n");
                }
        } else {
                printf("timer_create error!\n");
                exit(1);
        }
        return timerid;
}

void SignalHandler(int signo, siginfo_t * info, void *context)
{
        if (signo == SIGTIMER) {
		diveTime = diveTime + 1;
		drawLCD();
        }
        else if (signo == SIGINT) {
                timer_delete(timerid);
                printf("Ctrl + C cached!\n");
                exit(1);
        }
}
