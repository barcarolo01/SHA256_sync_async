#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include <dpu.h>
#include <time.h>
#include <dpu_log.h>

#ifndef DPU_EXE
#define DPU_EXE "./SHA256DPU"
#endif

#define NUM_MSG 16*64

char msg[MESSAGE_SIZE*16];
char digests[8*NRDPU*NR_TASKLETS];


static inline double my_clock(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC_RAW, &t);
  return (1.0e-9 * t.tv_nsec + t.tv_sec);
}

void readFile(unsigned char* msg, int index)
{
	char filename[50];
	snprintf(filename, sizeof(filename), "./MyFiles/file%d.txt",index);
	FILE* fp = fopen(filename,"r");
	int t=fread(msg,sizeof(unsigned char),MESSAGE_SIZE-9,fp); //'-9' required in order to get a total size of 1kB (with the padding bytes)
	fclose(fp);
}

int main()
{
	int d=0;
	uint32_t numDPU=0;
	uint32_t DPUs[NRDPU];
	uint32_t digests_DPU[8];
	uint32_t digests_HOST[8];
	for(int j=0;j<NRDPU;++j){ DPUs[j] = 0; }
	for(int j=0;j<8;++j){ digests_HOST[j] = 0; digests_DPU[j] = 0; }
	
	double DPUkernelTime=0, CPUDPUTime=0,DPUCPUtime=0,initTime=0,endTime=0,HostTime=0;
	int processedFileDPU=0, processedFileHost=0,FileTime=0,DPUtotalTime=0;
	struct dpu_set_t set, dpu,setR;
	double tmpTimer[7];
	uint32_t each_dpu,DPUtime,transferTime,numberOfTransfer,clockPerSec,SHAtimeDPU;
	uint32_t tmpH[8];
	uint32_t tmpD[8*24];
	
DPU_ASSERT(dpu_alloc(NRDPU, NULL, &set));	//Allocating DPUs
DPU_ASSERT(dpu_get_nr_dpus(set,&numDPU));
printf("DPU_ALLOCATE_ALL: %d\n",numDPU);

	if(NRDPU*16<=NUM_MSG)
	{
		printf("--Hashing\033[1;32m %d file\033[0m\t TOTAL = \033[1;32m%d MB\033[0m\n",NUM_MSG,(NUM_MSG/1024)*(MESSAGE_SIZE/1024));
		printf("Message size: %d kB\n",MESSAGE_SIZE/1024);
		printf("--Allocated %d DPUs\n",numDPU);
		printf("--Using %d tasklets\n",NR_TASKLETS);
		DPU_ASSERT(dpu_load(set, DPU_EXE, NULL));	//Loading DPU program
	}
	else
	{
		printf("\n\033[1;31mERROR\033[0m\n");
	}
	
	
	//INIT HOST HASHING
	tmpTimer[3] = my_clock();
	for(int i=0;i<NUM_MSG;++i)
	{
		readFile(msg,(i%128));
		uint32_t paddedLength = padding(msg,MESSAGE_SIZE-9);
		processedFileHost++;
	
		SHA256_t(msg,tmpH,MESSAGE_SIZE,0);
		for(int j=0;j<8;++j){digests_HOST[j] = digests_HOST[j] ^ tmpH[j];}	
	} 
	HostTime = my_clock() - tmpTimer[3];
	//END HOST HASHING
	printf("--Host hashing done\n");

	//INIT DPU HASHING
	initTime = my_clock();	//START Measuring performance
	for(processedFileDPU=0;processedFileDPU<NUM_MSG;d++)
	{

		printf("Processed files: %d/%d\n",processedFileDPU,NUM_MSG);
		DPU_FOREACH(set,dpu,each_dpu)
		{
			//Init buffer from files
			tmpTimer[6] = my_clock();
			for(int j=0;j<NR_TASKLETS;++j)
			{
				processedFileDPU++;
				readFile(msg+MESSAGE_SIZE*j,(d*NRDPU*NR_TASKLETS+NR_TASKLETS*each_dpu+j)%128);
				uint32_t t=padding(msg+MESSAGE_SIZE*j,MESSAGE_SIZE-9);
			}
			FileTime += (my_clock() - tmpTimer[6]);
			
			//Retrieving results, if available
			if(DPUs[each_dpu]==1)
			{
				//COPY FROM DPU
				dpu_sync(dpu);
				tmpTimer[2]=my_clock();
				DPU_ASSERT(dpu_prepare_xfer(dpu,tmpD));
				DPU_ASSERT(dpu_push_xfer(dpu,DPU_XFER_FROM_DPU,"hash_digests",0,8*NR_TASKLETS*sizeof(uint32_t),DPU_XFER_DEFAULT));
				DPUCPUtime += (my_clock() - tmpTimer[2]);		
				
				for(int k=0;k<8*NR_TASKLETS;++k){ digests_DPU[k%8] = digests_DPU[k%8] ^ tmpD[k]; }
			}
			else{	DPUs[each_dpu]=1; }	
			
			//COPY TO DPU
			tmpTimer[0] = my_clock();
			DPU_ASSERT(dpu_prepare_xfer(dpu,msg));
			DPU_ASSERT(dpu_push_xfer(dpu,DPU_XFER_TO_DPU,"msgs",0,MESSAGE_SIZE*NR_TASKLETS,DPU_XFER_DEFAULT));
			//DPU_ASSERT(dpu_copy_from(dpu,"hash_digests",0,tmpD,8*NR_TASKLETS*sizeof(uint32_t)));
			CPUDPUTime += (my_clock() - tmpTimer[0]);
			
			DPU_ASSERT(dpu_launch(dpu, DPU_ASYNCHRONOUS));
			printf("%d: Launched DPU %d\n",d,each_dpu);
			}
	}

	//RETRIEVING LAST RESULTS
	DPU_FOREACH(set,dpu,each_dpu)
	{
			dpu_sync(dpu);
			
			tmpTimer[2]=my_clock();
				DPU_ASSERT(dpu_copy_from(dpu,"DPUtime",0,&DPUtime,sizeof(uint32_t)));
			DPU_ASSERT(dpu_copy_from(dpu,"transferTime",0,&transferTime,sizeof(uint32_t)));
			DPU_ASSERT(dpu_copy_from(dpu,"SHAtime",0,&SHAtimeDPU,sizeof(uint32_t)));
			
			DPU_ASSERT(dpu_prepare_xfer(dpu,tmpD));
			DPU_ASSERT(dpu_push_xfer(dpu,DPU_XFER_FROM_DPU,"hash_digests",0,8*NR_TASKLETS*sizeof(uint32_t),DPU_XFER_DEFAULT));
			//DPU_ASSERT(dpu_copy_from(dpu,"hash_digests",0,tmpD,8*NR_TASKLETS*sizeof(uint32_t)));
		
			for(int k=0;k<8*NR_TASKLETS;++k){ digests_DPU[k%8] = digests_DPU[k%8] ^ tmpD[k]; }
			DPUCPUtime += (my_clock() - tmpTimer[2]);		
	}
	printf("DPU encryption done\n\n");
	endTime = my_clock();
	//END DPU HASHING
	DPU_ASSERT(dpu_free(set));

	int z=0, err=0;
	for(z=0;z<8 && err==0;++z){ if(digests_DPU[z]!=digests_HOST[z]) {err = 1;} }
	if(err==0){ printf("[\033[1;32mOK\033[0m] Digests are equals\n"); }
	else{ printf("[\033[1;31mERROR\033[0m] Digests are NOT equals: z=%d\n",z); }
	
	
	printf("HOST: %d DPU: %d\n\n",processedFileHost,processedFileDPU);
	
	printf("-CPU-DPU transfer time: %.1f ms.\n", 1000.0*(CPUDPUTime));
	printf("-DPU kernel time: %.1f ms.\n", 1000.0*DPUkernelTime);
	printf("\t One DPU measured kernel time: %.1f ms.\n", 1000.0*DPUtime);
	printf("-DPU-CPU transfer time: %.1f ms.\n\n", 1000.0*DPUCPUtime);
	printf("----Total host elapsed time: %.1f ms.\n", 1000.0*(endTime-initTime));
	printf("----Hashing time on HOST CPU: %.1f ms.\n", 1000.0*HostTime);
    return 0;
}
