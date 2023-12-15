#!/bin/bash
clear
if [ $1 -eq 1 ]
then
echo "SYNC"
dpu-upmem-dpurte-clang -DNR_TASKLETS=16 -DNRDPU=$2  -DSTACK_SIZE_DEFAULT=2048 -O3 SHA256DPU.c -o SHA256DPU
gcc --std=c11 -DNR_TASKLETS=16 -DNRDPU=$2 -DNUM_MSG=$3 -O3 SHA256hostSYNC.c -o SHA256hostSYNC `dpu-pkg-config --cflags --libs dpu`
./SHA256hostSYNC
fi
if [ $1 -eq 2 ]
then 
echo "ASYNC"
dpu-upmem-dpurte-clang -DNR_TASKLETS=16 -DNRDPU=$2  -DSTACK_SIZE_DEFAULT=2048 -O3 SHA256DPU.c -o SHA256DPU
gcc --std=c11 -DNR_TASKLETS=16 -DNRDPU=$2  -O3 SHA256hostASYNC.c -o SHA256hostASYNC `dpu-pkg-config --cflags --libs dpu`
./SHA256hostASYNC
fi

if [ $1 -eq 3 ]
then 
echo "ASYNC_RANK"
dpu-upmem-dpurte-clang -DNR_TASKLETS=16 -DNRDPU=$2  -DSTACK_SIZE_DEFAULT=2048 -O3 SHA256DPU.c -o SHA256DPU
gcc --std=c11 -DNR_TASKLETS=16 -DNRDPU=$2  -O3 SHA256hostASYNC_1.c -o SHA256hostASYNC_1 `dpu-pkg-config --cflags --libs dpu`
./SHA256hostASYNC_1
fi