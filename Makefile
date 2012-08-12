main: main.c
	 cc -o main main.c PCD8544.c -lrt -L/usr/local/lib -lwiringPi

clean:
	rm main
