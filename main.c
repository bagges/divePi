////////////////////////// divePi ///////////////////////////
// divePi is a DIY dive computer based on the raspberry pi //
// This software comes whitout any warranty and is not     //
// yet finished. And its not sure if it gets finished ;-)  //
/////////////////////////////////////////////////////////////
// This code is mostly based on the source code of         //
// the SBTC of Peter Rachow http://www.peter-rachow.de/    //
// and was modified by Markus Backes.                      //
// Thank you very much Peter!                              //
/////////////////////////////////////////////////////////////
// Filename: main.c                                        //
// Author: Peter Rachow / Markus Backes                    //
// Last Modified: 2012-08-13 22:15                         //
// Description: currently this main routine only shows     //
// predefined values on the Nokia 5110 display attached to //
// raspberry pi. currently there is no calculation done!   //
// The gpio pins were controlled with wiringPi library     //
// thanks to Gordon Henderson!                             //
// The LCD is controlled by a library of André Wussow from //
// http://www.binerry.de. Thank you for sharing.           //
/////////////////////////////////////////////////////////////

#include <time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <wiringPi.h>
#include <stdint.h>
#include <string.h>
#include <sys/sysinfo.h>
#include "binerry/libraries/c/PCD8544/PCD8544.h"
#include <math.h>
 
#define SIGTIMER     (SIGRTMAX)
 
timer_t SetTimer(int signo, int sec, int mode);
void SignalHandler(int signo, siginfo_t * info, void *context);
timer_t timerid;
void initDisplay(void);
void initTimer(void);
void drawLCD(void);
void get_temp_sensor(void);

// pin setup
int _din = 1;
int _sclk = 0;
int _dc = 2;
int _rst = 4;
int _cs = 3;
  
// lcd contrast 
int contrast = 60;
int diveTime = 0;

 //////////////////////////////////////////////////
// Dekompressionsrechnung & verwandte Variablen //
/////////////////////////////////////////////////

#define NCOMP 16   // Anzahl der Kompartimente des Buehlmannalgorithmus
#define FN2 0.78   // N2-Anteil im Atemgas                 
#define MAX_DECO_STEPS 8  //Max. Dekotiefe der 1. Stufe = 24m
#define MAXGASES 3

#define DIVE 1        //Werte der Variablen 'phase' um kenntlich
#define SURFACE 0     // zu machen, ob getaucht wird oder nicht
char phase;


// Gewebekonstanten fuer 16 Kompartimente  
// STICKSTOFF                              
float t05N2[] = {4, 8, 12.5, 18.5, 27, 38.3, 54.3, 77, 109, 146, 187, 239, 305, 390, 498, 635}; //HWZ in min.
float aN2[] = {1.2599, 1, 0.8618, 0.7562, 0.662, 0.5043, 0.441, 0.4, 
               0.375, 0.35, 0.3295, 0.3065, 0.2835, 0.261, 0.248, 0.2327};
float bN2[] = {0.505, 0.6514, 0.7222, 0.7825, 0.8126, 0.8434, 0.8693, 0.891, 
               0.9092, 0.9222, 0.9319, 0.9403, 0.9477, 0.9544, 0.9602, 0.9653};

// Kompartimente
float piN2[NCOMP]; 

// In dieses Array kommen die einzelnen Dekozeiten. Jedes Element 
// entspricht einer Dekostufe beginnend mit [0] = 3m
char decotime[MAX_DECO_STEPS] = {0, 0, 0, 0, 0, 0, 0, 0};
  
// 3 durch Anwender waehlbare Gasgemische aus O2 und N2 (Gas1 = Luft) 
unsigned char curgas = 0;
double figN2[MAXGASES] = {FN2, 0.36, 0};              // N2-Anteil in 3 Auswahlgasen    

int curdepth = 155, maxdepth = 228;// TEST VALUES!!!!       // Akt. und max. Tiefe [dm]                     
float temp_min = 0;                         // niedrigste Temperatur                        
float temp_maxdepth = 0;                    // Temperatur auf max Tiefe                     

double airp = 0.995;                  // Umgebungsluftdruck in bar am Tauchort        
double airp0 = 0.995;                 // Umgebungsluftdruck in bar auf NN             
double cabinp = 0.58;                 // Kabinendruck im Flugzeug in bar              
int altitude = 0;                     // Hoehe ueber NN                                
double curtemp = 2030; //TEST VALUES!!!!!                       // Aktuelle Temperatur                          
int deepest_decostep = 0;             // Tiefster Dekostopp in dm                     
int deco_minutes_total = 0;           // Gesamtdekozeit in min.                       
unsigned char f_cons;                 // Faktor fuer ab-Modifikation (10facher Wert)  
char show_ppN2 = 0;                   // ppN2 nach TG anzeigen fuer 16 Kompartimente   

int switchdepth = 20;                //Tiefe bei der zwischen OFP- und Tauchmodus umgeschaltet wird.

int ndt = 0;
// Temperatursensor auslesen 
void get_temp_sensor(void)
{
    	//curtemp zuweisen
	curtemp = 2030; // TEST TEMP
	if(temp_min > curtemp) {
		temp_min = curtemp;
	}

}

// Wasserdruck p.amb aus Tiefe depth  
// berechnen                          
float get_water_pressure(int depth)
{
    return depth * 0.1 + airp;
}

// Wassertiefe depth aus p.amb berechnen 
float get_water_depth(float pamb)
{
    return (pamb - airp) * 10;
}

// Inertgaspartialdruck im Gewebe berechnen 
// d: Tiefe in m  Intervall immer 10 sec.   
void calc_p_inert_gas(int d)
{
    unsigned char t1;
    float pamb = get_water_pressure(d) - 0.0627;

    for(t1 = 0; t1 < NCOMP; t1++) {
        piN2[t1] += (pamb * figN2[curgas] - piN2[t1]) * (1 - exp((-0.1667 / t05N2[t1]) * log(2)));
    }
}


// Errechnen der Restnullzeit 
int calc_ndt()
{
    char calcok = 0;      // Flag, ob Rechnung OK ist 
    unsigned char t1;

    int dp = curdepth * 0.1; // Wassertiefe in m 
    int t0min = 999;

    float te, xN2;
    float piigN2, pamb = get_water_pressure(dp) - 0.0627;

    piigN2 = pamb * figN2[curgas];

    for(t1 = 0; t1 < NCOMP; t1++)
    {
        // Anwendung der Logarithmusgleichung 
        if(piigN2  - piN2[t1] && figN2[curgas])
        {
            xN2 = -1 * ((airp / bN2[t1] + aN2[t1] - piN2[t1]) / (piigN2 - piN2[t1]) - 1);
            
            if(xN2 > 0) // Ist Logarithmieren moeglich? 
            {
                te = -1 * log(xN2) / log(2) * t05N2[t1]; 
                if(te < t0min)
                    t0min = te;
                calcok = 1;
            }
        }
    }

    if(calcok && dp > 10)
    {
        if(t0min > 0)
            return (int) t0min;
        else
            return 0;
    }    
    else
    {
        return -1;
    }        
}


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
                exit(1);// Temperatursensor auslesen 
void get_temp_sensor()
{
    //curtemp zuweisen
}
        }
 
        // Establish a handler to catch CTRL+C and use it for exiting
        sigaction(SIGINT, &sigact, NULL);
 
        timerid = SetTimer(SIGTIMER, 1000, 1);
}

void drawLCD(void) {

	//display depth
	char depthChar[20];
	snprintf(depthChar, 20, "D:%.1fm(%.1fm)", curdepth/(float)10, maxdepth/(float)10);

	// Display Dive Time
	char diveTimeChar[25];
	if(diveTime >= 60) {
		snprintf(diveTimeChar, 25, "DT:%ldm", (int)diveTime/60);
	} else {
		snprintf(diveTimeChar, 25, "DT:%lds", diveTime);
	}

	//Display temp
	char tempChar[25];
	snprintf(tempChar, 25, "T:%.1f(%.1f)", curtemp / (float)100, temp_min / (float)100);

	//display nullzeit
	char nullzeit[25];
	snprintf(nullzeit, 25, "N:%ld", ndt);

	//LCDclear();
	LCDdrawstring(0,0,depthChar);	
	LCDdrawstring(0,8,diveTimeChar);
	LCDdrawstring(0,16,tempChar);
	LCDdrawstring(0,24,nullzeit);
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
		LCDclear();
		LCDdisplay();
                printf("Ctrl + C cached!\n");
                exit(1);
        }
}
