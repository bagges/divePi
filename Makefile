main: main.c
	 cc -o main main.c binerry/libraries/c/PCD8544/PCD8544.c -lrt -lm -L/usr/local/lib -lwiringPi

clean:
	rm main
