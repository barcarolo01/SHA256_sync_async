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

#define NRRANKS NRDPU/64

#define NUM_MSG (16*NRDPU)*NRROUND

char msg[MESSAGE_SIZE*16*512];
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
	uint32_t ranks_bool[NRRANKS];
	uint32_t digests_DPU[8];
	uint32_t digests_HOST[8];
	for(int j=0;j<NRDPU;++j){ DPUs[j] = 0; }
	for(int j=0;j<NRRANKS;++j){ ranks_bool[j] = 0; }
	for(int j=0;j<8;++j){ digests_HOST[j] = 0; digests_DPU[j] = 0; }
	
	double DPUkernelTime=0, CPUDPUTime=0,DPUCPUtime=0,initTime=0,endTime=0,HostTime=0,FileTimeHOST=0,FileTimeDPU=0;
	int processedFileDPU=0, processedFileHost=0,DPUtotalTime=0;
	struct dpu_set_t rank,setRanks,set, dpu;
	double tmpTimer[7];
	uint32_t each_dpu,each_rank,DPUtime,transferTime,numberOfTransfer,clockPerSec,SHAtimeDPU;
	uint32_t tmpH[8];
	uint32_t tmpD[8*24];
	
DPU_ASSERT(dpu_alloc_ranks(NRRANKS, NULL, &setRanks));	//Allocating DPUs
//DPU_ASSERT(dpu_alloc_ranks(NRRANKS, NULL, &set));	//Allocating DPUs
DPU_ASSERT(dpu_get_nr_dpus(setRanks,&numDPU));
printf("Allocated: %d DPUs\n",numDPU);

	if(NRDPU*16<=NUM_MSG)
	{
		printf("--Hashing\033[1;32m %d file\033[0m\t TOTAL = \033[1;32m%d MB\033[0m\n",NUM_MSG,(NUM_MSG/1024)*(MESSAGE_SIZE/1024));
		printf("Message size: %d kB\n",MESSAGE_SIZE/1024);
		printf("--Allocated %d DPUs\n",numDPU);
		printf("--Using %d tasklets\n",NR_TASKLETS);
		
		DPU_ASSERT(dpu_load(setRanks, DPU_EXE, NULL));	//Loading DPU program
	}
	else
	{
		printf("\n\033[1;31mERROR\033[0m\n");
	}
	
	
	//INIT HOST HASHING
	tmpTimer[3] = my_clock();
	printf("NUM_MSG: %d\n",NUM_MSG);
	for(int i=0;i<NUM_MSG;++i)
	{
//		printf("A");
		processedFileHost++;
		
		tmpTimer[0]=my_clock();
		readFile(msg,(processedFileHost%9));
		FileTimeHOST += (my_clock() - tmpTimer[0]);
		
		uint32_t paddedLength = padding(msg,MESSAGE_SIZE-9);
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
		DPU_RANK_FOREACH(setRanks,rank,each_rank)
		{
/*			
			if(ranks_bool[each_rank]==1)
			{
				DPU_FOREACH(rank,dpu,each_dpu)
				{
					DPU_ASSERT(dpu_prepare_xfer(dpu,tmpD));
					DPU_ASSERT(dpu_push_xfer(dpu,DPU_XFER_FROM_DPU,"hash_digests",0,8*NR_TASKLETS*sizeof(uint32_t),DPU_XFER_DEFAULT));
					//DPU_ASSERT(dpu_copy_from(dpu,"hash_digests",0,tmpD,8*NR_TASKLETS*sizeof(uint32_t)));

					for(int k=0;k<8*NR_TASKLETS;++k){ digests_DPU[k%8] = digests_DPU[k%8] ^ tmpD[k]; }
				}
			}
			else
			{
				ranks_bool[each_rank] = 1;
			}
*/			
			
			tmpTimer[2] = my_clock(); 
			printf("FOREACH_RANK: %d\n",each_rank);
			DPU_FOREACH(rank,dpu,each_dpu)
			{
//				printf("RANK=%d  DPU=%d\n",each_rank,each_dpu);
				//File Read
				for(int j=0;j<NR_TASKLETS;++j)
				{
					processedFileDPU++;
					tmpTimer[0] = my_clock();
					readFile(msg+MESSAGE_SIZE*j,processedFileDPU%9);
					FileTimeDPU += (my_clock() - tmpTimer[0]);
					
					uint32_t t=padding(msg+MESSAGE_SIZE*j,MESSAGE_SIZE-9);
				}
			
			//Copy to DPU
			//DPU_ASSERT(dpu_copy_to(dpu,"msgs",0,msg,MESSAGE_SIZE*NR_TASKLETS));
			DPU_ASSERT(dpu_prepare_xfer(dpu,msg+MESSAGE_SIZE*16*each_dpu));	//Prepare
			DPU_ASSERT(dpu_push_xfer(dpu,DPU_XFER_TO_DPU,"msgs",0,MESSAGE_SIZE*NR_TASKLETS,DPU_XFER_ASYNC));	//Transfer		
			}	//FOR_EACH
		dpu_sync(rank);
		DPU_ASSERT(dpu_launch(rank, DPU_ASYNCHRONOUS));
		CPUDPUTime += my_clock() - tmpTimer[2];
	} //FOR_EACH_RANK
		
tmpTimer[2]=my_clock();		
DPU_RANK_FOREACH(setRanks,rank,each_rank){		
		DPU_FOREACH(rank,dpu,each_dpu)
		{
		DPU_ASSERT(dpu_prepare_xfer(dpu,tmpD));
		DPU_ASSERT(dpu_push_xfer(dpu,DPU_XFER_FROM_DPU,"hash_digests",0,8*NR_TASKLETS*sizeof(uint32_t),DPU_XFER_DEFAULT));
		//DPU_ASSERT(dpu_copy_from(dpu,"hash_digests",0,tmpD,8*NR_TASKLETS*sizeof(uint32_t)));

		for(int k=0;k<8*NR_TASKLETS;++k){ digests_DPU[k%8] = digests_DPU[k%8] ^ tmpD[k]; }
		}
}
DPUCPUtime=my_clock() - tmpTimer[2];
	}	//FOR
	endTime = my_clock();
	printf("DPU encryption done\n\n");
	
	//END DPU HASHING

	int z=0, err=0;
	for(z=0;z<8 && err==0;++z){ if(digests_DPU[z]!=digests_HOST[z]) {err = 1;} }
	if(err==0){ printf("[\033[1;32mOK\033[0m] Digests are equals\n"); }
	else{ printf("[\033[1;31mERROR\033[0m] Digests are NOT equals: z=%d\n",z); }
	printf("DPU: %08X\nHOST:%08X\n\n",digests_DPU[0],digests_HOST[0]);
	

	printf("HOST: %d DPU: %d\n\n",processedFileHost,processedFileDPU);
	printf("-CPU-DPU transfer time: %.1f ms.\n", 1000.0*(CPUDPUTime-FileTimeDPU));
	printf("-DPU kernel time: %.1f ms.\n", 1000.0*DPUkernelTime);
	printf("\t One DPU measured kernel time: %.1f ms.\n", 1000.0*DPUtime);
	printf("-DPU-CPU transfer time: %.1f ms.\n\n", 1000.0*DPUCPUtime);
	printf("-FileTimeHOST: %.1f \t FileTimeDPU: %.1f\n\n", 1000.0*FileTimeHOST,1000.0*FileTimeDPU);
	printf("----Total host elapsed time: %.1f ms.\n", 1000.0*(endTime-initTime));
	printf("----Hashing time on HOST CPU: %.1f ms.\n", 1000.0*HostTime);
    return 0;
}
