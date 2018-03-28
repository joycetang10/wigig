#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>
#include <sys/time.h>

#include "iconv.h"
#include "pthread.h"
#include "speedtest.h"

#include "Common/mrloopsdkheader.h"
#include "mrloopbf_release.h"

#define BUFSIZE  4096
#define SCREEN_CLEAR "\033[2J"

bool s_index = false;
bool check_RF_status = false;
struct tm last_loctime;
struct tm last_ch;
uint32_t last_MAC_Rx_Total = 0;
long bits;
//add to store time in nsec
struct timespec last_nsec = {0,0};
int cur_MCS = 1;
bool change = false;

uint32_t last_ACK = 0;
uint32_t last_Tx_PKT = 0;

pthread_t thread;
pthread_t show_bitrate;
pthread_t getRFstatus;
pthread_t waitChange;

ML_RF_INF ML_RF_Record;

struct tm *get_local_time()
{
	time_t curtime;

	curtime = time (NULL);
	return localtime(&curtime);
}
//nsec
struct timespec *get_nsec()
{
	struct timespec *now_nsec = (struct timespec*)malloc(sizeof(struct timespec));
	clock_gettime(CLOCK_MONOTONIC, now_nsec);
	return now_nsec;
}
void readRFPackets(FILE *fp, ML_RF_INF Packets)
{
	struct tm *loctime = get_local_time();
	//get nsec
	struct timespec *new_nsec = get_nsec();
	double diff_time = 0;
	//diff for nsec
	double diff_in_nsec = 0;
	uint32_t n_detect = Packets.PHY_Rx_SC_PKT + Packets.PHY_Rx_CP_PKT;
	uint32_t n_pass = Packets.PHY_Total_Rx_Count - Packets.PHY_RX_FCS_Err;
	uint32_t n_error= n_detect - n_pass;
	
	uint32_t diff_ACK = 0;
	uint32_t diff_Tx_PKT = 0;

	if(n_detect == 0)
		n_detect = 1;

	if (last_MAC_Rx_Total == 0) { 
		last_loctime = *loctime;
		//nsec
		last_nsec = *new_nsec;
		last_ch = *loctime;
	}
	else {
		diff_time = difftime(mktime(loctime), mktime(&last_loctime));
		last_loctime = *loctime;
		//nsec
		diff_in_nsec = ((double)new_nsec->tv_sec + 1.0e-9 * new_nsec->tv_nsec) - ((double)last_nsec.tv_sec + 1.0e-9 * last_nsec.tv_nsec);
		last_nsec = *new_nsec;
		//if ((int)difftime(mktime(loctime),mktime(&last_ch)) > 30){
		//	change = true;
		//	last_ch = *loctime;
		//}
	}

	if (last_ACK == 0 && last_Tx_PKT == 0){
		diff_ACK = Packets.MAC_Total_Ack;
		diff_Tx_PKT = Packets.MAC_Tx_Total;
	}
	else{
		diff_ACK = Packets.MAC_Total_Ack - last_ACK;
		diff_Tx_PKT = Packets.MAC_Tx_Total - last_Tx_PKT;
	}

	if (last_MAC_Rx_Total != 0)
		printf("\033[9;0H ");
		fputs(asctime (loctime),stdout);
		printf("\033[10;0H MAC Throughput: %lf\n", ((Packets.MAC_Rx_Total - last_MAC_Rx_Total) * BUFSIZE * 8 / (1024 * 1024)) / diff_time);
		printf("\033[10;40H MAC Throughput(nsec): %lf \n", ((Packets.MAC_Rx_Total - last_MAC_Rx_Total) * BUFSIZE * 8 / (1024 * 1024)) / diff_in_nsec);
	printf("\033[11;0H PHY COUNTERS: \n");
	printf("\033[11;40H diff:");
	fputs(asctime(&last_ch),stdout); 
	printf("\033[12;3H Total Tx Counter: %u\n", Packets.PHY_Total_Tx_Count);
	printf("\033[13;3H Total Rx Counter: %u\n", Packets.PHY_Total_Rx_Count);
	printf("\033[14;3H Rx CP PKT: %u\n", Packets.PHY_Rx_CP_PKT);
	printf("\033[15;3H Rx SC PKT: %u\n", Packets.PHY_Rx_SC_PKT);

	printf("\033[16;3H PER: %1.3f\n", (float)n_error / (float)n_detect);
	printf("\033[17;3H RX STF: %1.3f\n", (float)Packets.PHY_Rx_STF_Err / (float)n_detect);
	printf("\033[18;3H RX HCS: %1.3f\n", (float)Packets.PHY_Rx_HCS_Err / (float)n_detect);
	printf("\033[19;3H RX FCS: %1.3f\n\n", (float)Packets.PHY_RX_FCS_Err / (float)n_detect);

	printf("\033[12;40H RX EVM(dBm): %d\n", Packets.PHY_Rx_EVM);
	printf("\033[13;40H RX SNR(dBm): %d\n", Packets.PHY_RX_SNR);
	printf("\033[14;40H RX RSSI(dBm): %d\n", Packets.PHY_RSSI);
	printf("\033[15;40H RX RCPI(dBm): %d\n", Packets.PHY_RCPI);
	printf("\033[16;40H AGC GAIN: %d\n\n", Packets.PHY_AGC_Gain);
	printf("\033[17;40H MCS: %d\n\n", Packets.MCS);
	printf("\033[18;40H PLR: %1.3f\n", (float)(diff_Tx_PKT - diff_ACK) / (float)diff_Tx_PKT );

	printf("\033[21;0H MAC COUNTERS: \n");
	printf("\033[22;3H Total Tx: %u\n", Packets.MAC_Tx_Total);
	printf("\033[23;3H Total Rx: %u\n", Packets.MAC_Rx_Total);
	printf("\033[24;3H Total Fail: %u\n", Packets.MAC_Total_Fail);
	printf("\033[25;3H Total Ack: %u\n", Packets.MAC_Total_Ack);
	printf("\033[26;3H Total Tx Done: %u\n\n", Packets.MAC_Total_Tx_Done);
	fflush(stdout);

	if (fp != stdout) {
		fputs(asctime (loctime), fp);
		fprintf(fp, "\n");
		fprintf(fp, "rx_diff = %u\ttime_diff = %lf\t",Packets.MAC_Rx_Total - last_MAC_Rx_Total, diff_time);
		//nsec
		fprintf(fp, "time_diff(nsec) = %lf\t", diff_in_nsec);
		if (last_MAC_Rx_Total != 0) 
		{	fprintf(fp, "MAC_Throughput: %lf\t",((Packets.MAC_Rx_Total - last_MAC_Rx_Total) * BUFSIZE * 8  / (1024 * 1024)) / diff_time);
			fprintf(fp, "MAC_Throughput(nsec): %lf\t", ((Packets.MAC_Rx_Total - last_MAC_Rx_Total) * BUFSIZE * 8  / (1024 * 1024)) / diff_in_nsec);
		}
		fprintf(fp,"[PHY_COUNTERS]\t"); 
		fprintf(fp,"Total_Tx_Counter: %u\t", Packets.PHY_Total_Tx_Count);
		fprintf(fp,"Total_Rx_Counter: %u\t", Packets.PHY_Total_Rx_Count);
		fprintf(fp,"Rx_CP_PKT: %u\t", Packets.PHY_Rx_CP_PKT);
		fprintf(fp,"Rx_SC_PKT: %u\t", Packets.PHY_Rx_SC_PKT); 

		fprintf(fp,"PER: %1.3f\t", (float)n_error / (float)n_detect);
		fprintf(fp,"PLR: %1.3f\t", (float)(diff_Tx_PKT - diff_ACK) / (float)diff_Tx_PKT );
		fprintf(fp,"RX_STF: %1.3f\t", (float)Packets.PHY_Rx_STF_Err / (float)n_detect);
		fprintf(fp,"RX_HCS: %1.3f\t", (float)Packets.PHY_Rx_HCS_Err / (float)n_detect);
		fprintf(fp,"RX_FCS: %1.3f\t", (float)Packets.PHY_RX_FCS_Err / (float)n_detect);

		fprintf(fp,"RX_EVM(dBm): %d\t", Packets.PHY_Rx_EVM);
		fprintf(fp,"RX_SNR(dBm): %d\t", Packets.PHY_RX_SNR);
		fprintf(fp,"RX_RSSI(dBm): %d\t", Packets.PHY_RSSI);
		fprintf(fp,"RX_RCPI(dBm): %d\t", Packets.PHY_RCPI);
		fprintf(fp,"AGC_GAIN: %d\t", Packets.PHY_AGC_Gain);
		fprintf(fp,"MCS: %d\t", Packets.MCS);

		fprintf(fp,"[MAC COUNTERS]\t");
		fprintf(fp,"Total_Tx: %u\t", Packets.MAC_Tx_Total);
		fprintf(fp,"Total_Rx: %u\t", Packets.MAC_Rx_Total);
		fprintf(fp,"Total_Fail: %u\t", Packets.MAC_Total_Fail);
		fprintf(fp,"Total_Ack: %u\t", Packets.MAC_Total_Ack);
		fprintf(fp,"Total_Tx_Done: %u\t", Packets.MAC_Total_Tx_Done);
		fprintf(fp, "\n");
		fprintf(fp,"last_Tx_PKT: %u\t", last_Tx_PKT);
		fprintf(fp,"last_ACK: %u\n", last_ACK);
		
	}
	last_MAC_Rx_Total = Packets.MAC_Rx_Total;
	last_ACK = Packets.MAC_Total_Ack;
	last_Tx_PKT = Packets.MAC_Tx_Total;
}
int get_RF_status(FILE *fp)
{
	uint8_t *buf = new uint8_t[BUFSIZE];
	int length = BUFSIZE;

	/*Send request to RF*/
	ML_SendRFStatusReq();
	/*Get RF data buf*/
	ML_Receiver(buf, &length);
	/*Decode RF status packet*/
	if (ML_DecodeRFStatusPacket(buf, &ML_RF_Record)) {
		readRFPackets (fp, ML_RF_Record);
		memset (&ML_RF_Record, 0 , sizeof (ML_RF_INF));
	}
	else return 1;
	return 0;
}
void log_RF_status()
{
	FILE *fp = fopen("RF_StatusInfo.log", "a+");

	while(get_RF_status(fp));

	fclose(fp);
}
int Initial_setting (uint8_t speed, uint8_t rule)
{
	int err = ML_Init();
	if (err != 1) {
		return -1;
	} else {
		if (!ML_SetSpeed(speed)) {
			perror("ML_SetSpeed");
			exit (EXIT_FAILURE);
		}
		if (!ML_SetMode(rule)) {
			perror("ML_SetMode");
			exit (EXIT_FAILURE);
		}
		log_RF_status();
		s_index = true;
	} 
	return 0;
}
void *Pull_RF_status(void *ptr)
{
	while(s_index){
		if (check_RF_status) {
			while(get_RF_status(stdout)); 
			check_RF_status = false;			
		}
	}
	return ((void *)0);
}	
int connecting_screen(int s)
{
	char input[BUFSIZE] = {'\0'};
	int speed = s;

	fprintf(stdout, SCREEN_CLEAR);
	fprintf(stdout, "\033[1;0H");
	fprintf(stdout, "Speed Set: [1-7] (0 to exit)\nFor RF Status info input \"rf\"\n");
	
	scanf("%s", input);	
	if (isdigit(input[0])) {
		speed = atoi(input);
		if (speed == 0) return 0;
		else if (speed < 1 || speed > 7) {
			fprintf(stdout, "Error speed setting!\n");
			return 0;
		} else {
			if (!ML_SetSpeed((uint8_t)speed)) {
				perror ("ML_SetSpeed");
				return -1;
			}
			log_RF_status();
		}
	} else if (strcmp(input, "rf") == 0) check_RF_status = true;
	

	else fprintf(stdout, "Error input!\n");

	return (speed);
}

void* WaitChange(void *ptr){
	int new_MCS = 2;
	while(s_index){
		if(change==true){
			fprintf(stdout,"\033[5;70H change from %d to %d\n",cur_MCS, new_MCS);
			if(!ML_SetSpeed((uint8_t)new_MCS)){
				perror("ML_SetSpeed");
				return ((void *)-1);
			}
			cur_MCS = new_MCS;
			new_MCS++;
			if(new_MCS == 8)	new_MCS = 1;

			change = false;
			log_RF_status();
		}
	}

	return ((void *)0);

}
int main(int argc, char *argv[])
{
	int speed, rule, err = 0;

	switch (argc) {
		case 3:
			speed = atoi(argv[1]);
			if (speed < 1 || speed > 7) {
				fprintf(stderr, "Error speed setting!\n");
				exit (0);
			}
			rule = atoi(argv[2]);
			if (rule != 1 && rule != 2) {
				fprintf(stderr, "Error rule setting!\n");
				exit (0);
			}
			break;
		default:
			fprintf(stdout, "usage: sudo SpeedTest_rf [Speed set: 1-7 (mcs)] [Rule set: 1.PCP | 2.STA]\n");
			exit (0);
	}
	err = Initial_setting((uint8_t)speed, (uint8_t)rule);
	if (err != -1) {
		if (rule == 1) {
			pthread_create(&thread, NULL, SpeedRx, NULL);
		} else {
			pthread_create(&thread, NULL, SpeedTx, NULL);
		}
		pthread_create(&waitChange, NULL, WaitChange, NULL);
		pthread_create(&show_bitrate, NULL, ShowBitrate, NULL);
		pthread_create(&getRFstatus, NULL, Pull_RF_status, NULL);
		do {
			speed = connecting_screen(speed);
		} while (speed > 0);

		s_index = false;
		pthread_join(thread, NULL);
		pthread_join(show_bitrate, NULL);
		pthread_join(getRFstatus, NULL);
		pthread_join(waitChange, NULL);
		log_RF_status();
	} else
		perror("ML_Init:");

	return 0;
}



void* ShowBitrate(void *ptr){

	long bitrate = 0;
	int counter = 0;
	FILE *fp = fopen("RFandRate.log", "a+");
	while(s_index){
		bitrate = ((bits * 8) / (1024 *1024));
		fprintf(stdout,"\033[8;0H  %ld Mbp/s\n", bitrate);
		fflush(stdout);
		//if( counter %  10== 0 ){
			fprintf( fp, "count%d:\t%ld\t", counter, bitrate);
			while(get_RF_status(fp));
		//}
		bits = 0;
		counter++;
		sleep(1);
	}

	fclose(fp);
	return ((void *)0);
}

void* SpeedTx(void *ptr){

	unsigned char* buf = (unsigned char*) malloc(BUFSIZE * 4  * sizeof(char));
	memset(buf, 0, BUFSIZE * 4);
	int length = BUFSIZE *4, status;
	//int count2Stop = 3;

	while(s_index){
		status = ML_Transfer(buf, length);
		if(status > 0){
			//printf("\033[5;40H tran phase = %d\n", count2Stop);
			bits += length;
			//count2Stop--;			
			//if(count2Stop == 0){
			//	printf("\033[6;40H done of tran\n");
			//	break;
			//}
			
		}
	}
	free(buf);
	return ((void *)0);
}

void* SpeedRx(void *ptr){

	uint8_t* buf = (uint8_t*) malloc(BUFSIZE * 4);
	memset(buf, 0, BUFSIZE * 4);
	int status;
	int length;

	while(s_index){
		length = BUFSIZE * 4;
		status = ML_Receiver(buf, &length);
		if(status > 0){
			bits += length;
		}
		else{
			fprintf(stdout,"Rx fail return:%d", status);
		}
	}
	free(buf);
	return ((void *)0);
}
