#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>

#define DEBUG 0

#define PAGEMAP_ENTRY 8
#define GET_BIT(X,Y) (X & ((uint64_t)1<<Y)) >> Y
#define GET_PFN(X) X & 0x7FFFFFFFFFFFFF
#define init_module(mod, len, opts) syscall(__NR_init_module, mod, len, opts)
#define delete_module(name, flags) syscall(__NR_delete_module, name, flags)

static int pageSize;

const int __endian_bit = 1;
#define is_bigendian() ( (*(char*)&__endian_bit) == 0 )

typedef struct VIRTUAL_ADDR_T{
	unsigned long vmStart;
	unsigned long vmEnd;
	unsigned long long pfn;
	unsigned long long flags;
	int zoneId;
	char *name;
	struct VIRTUAL_ADDR_T *next;
} VIRTUAL_ADDR_T;


#define MAX_CMD_LEN 64

typedef struct MEMORY_ZONE_T{
	unsigned long long pfn;
	unsigned long long flags;
	int zoneId;
	char *name;
	struct MEMORY_ZONE_T* next;
} MEMORY_ZONE_T;

int i, c, pid, status;
unsigned long virt_addr; 
uint64_t file_offset;
char path_buf [0x100] = {};
FILE *pagemapFileObj = NULL;
char *end;

//for storing pfn values ===============Non
unsigned long long *pfn_arr; //array of pfn
size_t pfn_count; //size
//end======================================

VIRTUAL_ADDR_T* read_map();
MEMORY_ZONE_T* checkResultFromPFN2Zone(int cnt);
VIRTUAL_ADDR_T* read_pagemaps(VIRTUAL_ADDR_T *rootAddr);
VIRTUAL_ADDR_T* merge_zone(VIRTUAL_ADDR_T *rootAddr, MEMORY_ZONE_T *rootMemoryAddr);
unsigned long long read_pagemap(unsigned long virt_addr);
int call_pfn2zone();
unsigned long page2kb(unsigned long startPos, unsigned long endPos);
void summary(VIRTUAL_ADDR_T *rootAddr);

int main(int argc, char ** argv){
	VIRTUAL_ADDR_T *rootAddr;
	MEMORY_ZONE_T *rootMemoryAddr;

	if(argc!=2){
		fprintf(stderr, "Argument number is not correct!\n read_map PID\n");
		return EXIT_FAILURE;
	}
	if(!memcmp(argv[1],"self",sizeof("self"))){
		pid = -1;
	}else{
		pid = strtol(argv[1],&end, 10);
		if (end == argv[1] || *end != '\0' || pid<=0){ 
			fprintf(stderr, "PID must be a positive number or 'self'\n");
			return EXIT_FAILURE;
		}
	}

	pageSize = getpagesize();

	rootAddr = read_pagemaps(read_map());
	/*Prepare list of PFN to find zone*/
	if(call_pfn2zone() == EXIT_FAILURE){
		fprintf(stderr, "Error while attemp to call kernel\n");
	}
	rootMemoryAddr = checkResultFromPFN2Zone(pfn_count);
	if(rootMemoryAddr != NULL){
		merge_zone(rootAddr, rootMemoryAddr);
	}
	summary(rootAddr);
	return 0;
}

VIRTUAL_ADDR_T* read_pagemaps(VIRTUAL_ADDR_T *rootAddr){
	size_t i = 0;
	VIRTUAL_ADDR_T *currAddr;

	for (currAddr = rootAddr, pfn_count = 0; currAddr != NULL; currAddr = currAddr->next, ++pfn_count); //count for malloc
	pfn_arr = (unsigned long long*)calloc(pfn_count,sizeof(unsigned long long));
	pfn_count = 0;

	for (currAddr = rootAddr; currAddr != NULL; currAddr = currAddr->next, ++i){
		if(DEBUG) printf("%zu. 0x%lx - 0x%lx\next", i, currAddr->vmStart, currAddr->vmEnd);
		currAddr->pfn = read_pagemap(currAddr->vmStart);
		if(DEBUG) printf("==============================================\n");
		if(currAddr->pfn != 0 && currAddr->pfn != ULONG_MAX){
			pfn_arr[pfn_count] = currAddr->pfn;
			++pfn_count;
		}
	}	

	fclose(pagemapFileObj);
	return rootAddr;
}

VIRTUAL_ADDR_T* read_map(){
	int pathBufLen;
	VIRTUAL_ADDR_T *currAddr, *previousAddr, *rootAddr;
	FILE *fp;
	char *line = NULL, *endStr = NULL;
	size_t len = 0;
	ssize_t read;
	unsigned long vmStart, vmEnd;

	rootAddr = NULL;
	previousAddr = NULL;

	if(pid != -1)
		pathBufLen = sprintf(path_buf, "/proc/%u/maps", pid);
	else
		pathBufLen = sprintf(path_buf, "/proc/self/maps");

	if(pathBufLen < 0)
		perror("Error while attemp to format the reader: ");

	fp = fopen(path_buf, "r");
	if (fp == NULL){
		perror("Error while reading process map: ");
		exit(EXIT_FAILURE);
	}

	while ((read = getline(&line, &len, fp)) != -1) {
		//vmStart = strtoul (line, &endStr, 16);
		if(sscanf(line, "%lx%*c%lx", &vmStart, &vmEnd) < 2){
			fprintf(stderr, "Error while reading address");
			exit(EXIT_FAILURE);
		}

		currAddr = (VIRTUAL_ADDR_T*) malloc(sizeof(VIRTUAL_ADDR_T));
		if(currAddr == NULL){
			perror("Error while allocating address list: ");
			exit(EXIT_FAILURE);
		}

		currAddr->vmStart = vmStart;
		currAddr->vmEnd = vmEnd;
		currAddr->pfn = 0;
		currAddr->zoneId = -1;
		currAddr->name = NULL;
		currAddr->next = NULL;

		if(previousAddr == NULL){
			previousAddr = currAddr;
		}else{
			previousAddr->next = currAddr;
			previousAddr = currAddr;
		}
		if(rootAddr == NULL)	rootAddr = currAddr;
	}

	fclose(fp);
	if (line)
		free(line);
	return rootAddr;
}

unsigned long long read_pagemap(unsigned long virt_addr){
	int pathBufLen;
	unsigned char c_buf[PAGEMAP_ENTRY];
	unsigned long long read_val;

	if(pagemapFileObj == NULL){

		printf("Big endian? %d\n", is_bigendian());
		if(pid != -1)
			pathBufLen = sprintf(path_buf, "/proc/%u/pagemap", pid);
		else
			pathBufLen = sprintf(path_buf, "/proc/self/pagemap");

		if(pathBufLen < 0)
			perror("Error while attemp to format the reader: ");

		pagemapFileObj = fopen(path_buf, "rb");
		if(!pagemapFileObj){
			printf("Error! Cannot open %s : ", path_buf);
			fflush(stdout);
			perror("");
			exit(EXIT_FAILURE);
		}
	}else{
		rewind(pagemapFileObj);
	}
	
	//Shifting by virt-addr-offset number of bytes
	//and multiplying by the size of an address (the size of an entry in pagemap file)
	file_offset = virt_addr / pageSize * PAGEMAP_ENTRY;

	if(DEBUG) printf("Vaddr: 0x%lx, Page_size: %d, Entry_size: %d\n", virt_addr, getpagesize(), PAGEMAP_ENTRY);
	if(DEBUG) printf("Reading %s at 0x%llx\n", path_buf, (unsigned long long) file_offset);

	status = fseek(pagemapFileObj, file_offset, SEEK_SET);
	if(status){
		perror("Failed to do fseek!");
		return -1;
	}
	errno = 0;
	read_val = 0;
	
	if(DEBUG) printf("Page map chunks -> ");
	for(i=0; i < PAGEMAP_ENTRY; i++){
		c = getc(pagemapFileObj);
		if(c==EOF){
			if(DEBUG) printf("\nReached end of the file\n");
			return 0;
		}
		if(is_bigendian())
			c_buf[i] = c;
		else
			c_buf[PAGEMAP_ENTRY - i - 1] = c;
		if(DEBUG) printf("[%d]0x%x ", i, c);
	}
	for(i=0; i < PAGEMAP_ENTRY; i++){
		read_val = (read_val << 8) + c_buf[i];
	}
	if(DEBUG) printf("\n");
	if(DEBUG) printf("Result: 0x%llx\n", (unsigned long long) read_val);
	if(GET_BIT(read_val, 63)){
		if(DEBUG) printf("PFN: 0x%llx\n",(unsigned long long) GET_PFN(read_val));

		if(GET_PFN(read_val)!=0){			
			return (unsigned long long) GET_PFN(read_val);
		}
	}else{
		if(DEBUG) printf("Page not present\n");
		return 0;
	}
	if(GET_BIT(read_val, 62)){
		if(DEBUG) printf("Page swapped\n");
		return ULONG_MAX;
	}
	return 0;
}

int call_pfn2zone(){
	int pfn2zoneFD = 0;
	struct stat pfn2zoneStat;
	size_t pfn2zoneSize;
	void *pfn2zoneImage;
	char temp[32];
	char pfn_string[5000];
	size_t i;

	pfn2zoneFD = open("pfn2zone.ko", O_RDONLY);	
	fstat(pfn2zoneFD, &pfn2zoneStat);
	pfn2zoneSize = pfn2zoneStat.st_size;

	pfn2zoneImage = malloc(pfn2zoneSize);
	read(pfn2zoneFD, pfn2zoneImage, pfn2zoneSize);
	close(pfn2zoneFD);

	strcpy(pfn_string,"pfn_array=");
	for(i=0;i<pfn_count;i++){
		if(i==pfn_count-1)
			sprintf(temp,"0x%llx", pfn_arr[i]);
		else
			sprintf(temp,"0x%llx,",pfn_arr[i]);

		strcat(pfn_string,temp);		
	}

	pfn_string[strlen(pfn_string)+1]='\0';

	if (init_module(pfn2zoneImage, pfn2zoneSize, pfn_string) != 0) {
		perror("init_module");
		return EXIT_FAILURE;
	}
	free(pfn2zoneImage);
	if (delete_module("pfn2zone", O_NONBLOCK) != 0) {
		perror("delete_module");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

MEMORY_ZONE_T* checkResultFromPFN2Zone(int cnt){
	MEMORY_ZONE_T *rootAddr = NULL, *currAddr = NULL, *previousAddr = NULL;
	FILE *fp;
	char buf[1035];
	char cmd[MAX_CMD_LEN];	
	int cmdLen;
	
	char zoneName[16];
	unsigned long long pfn;
	unsigned long long flags;
	int zoneId;	
	
	cmdLen = snprintf(cmd, MAX_CMD_LEN, "dmesg | grep pfn2zone_by_Joe | tail -%d", cnt);
	if(cmdLen <= 0 || cmdLen >= MAX_CMD_LEN){
		fprintf(stderr, "checkResultFromPFN2Zone: Cannot format command\n");
		return NULL;
	}
	
	fp = popen(cmd, "r");
	if(fp == NULL){
		perror("checkResultFromPFN2Zone: Cannot read dmesg ->");
		return NULL;
	}
	
	while (fgets(buf, sizeof(buf)-1, fp) != NULL) {
		if(DEBUG) printf("RAW: %s\t\t", buf);
		if(sscanf(
			strchr(buf, ']'), 
			"%*s %*s %llx %llx %d %s", 
			&pfn,
			&flags,
			&zoneId,
			zoneName
		) < 4){
			continue; //ignore defect
		}else{
			currAddr = (MEMORY_ZONE_T*) malloc(sizeof(MEMORY_ZONE_T));
			if(currAddr == NULL){
				perror("Error while allocating address list: ");
				exit(EXIT_FAILURE);
			}
			
			currAddr->pfn = pfn;
			currAddr->flags = flags;
			currAddr->zoneId = zoneId;
			currAddr->name = (char*)malloc(sizeof(char)*strlen(zoneName));
			strncpy(currAddr->name, zoneName, strlen(zoneName));
			
			if(DEBUG) printf("pfn=%llx, zone ID=%d, zone Name=%s\n", currAddr->pfn, currAddr->zoneId, currAddr->name);
			
			currAddr->next = NULL;
			
			if(previousAddr == NULL){
				previousAddr = currAddr;
			}else{
				previousAddr->next = currAddr;
				previousAddr = currAddr;
			}
			if(rootAddr == NULL)	
				rootAddr = currAddr;
		}	
	}
	
	pclose(fp);
	
	return rootAddr;
}

VIRTUAL_ADDR_T* merge_zone(VIRTUAL_ADDR_T *rootAddr, MEMORY_ZONE_T *rootMemoryAddr){
	size_t i = 0;
	VIRTUAL_ADDR_T *currAddr;
	MEMORY_ZONE_T *currMemoryAddr, *prevMemoryAddr;
	for (currAddr = rootAddr; currAddr != NULL; currAddr = currAddr->next, ++i){
		if(currAddr->pfn != 0 && currAddr->pfn != ULONG_MAX){
			for (currMemoryAddr = rootMemoryAddr; currMemoryAddr != NULL;){
				if(currMemoryAddr->pfn == currAddr->pfn){
					currAddr->flags = currMemoryAddr->flags;
					currAddr->zoneId = currMemoryAddr->zoneId;
					currAddr->name = currMemoryAddr->name;
					prevMemoryAddr = currMemoryAddr;
					currMemoryAddr = currMemoryAddr->next;
					free(prevMemoryAddr);
				}else{
					currMemoryAddr = currMemoryAddr->next;
				}
			}
		}
	}

	return rootAddr;
}

unsigned long page2kb(unsigned long startPos, unsigned long endPos){
	return ((endPos - startPos)) >> 10;
}

void summary(VIRTUAL_ADDR_T *rootAddr){
	size_t i = 0;
	VIRTUAL_ADDR_T *currAddr;
	printf("  #           VM Address        Size(KB)   PFN    Zone#      Page Flag  \n");
	for (currAddr = rootAddr; currAddr != NULL; currAddr = currAddr->next, ++i){
		printf(
			"%3zu 0x%08lx-0x%08lx %6lu ", 
			i, 
			currAddr->vmStart, currAddr->vmEnd,
			page2kb(currAddr->vmStart, currAddr->vmEnd)
		);
		if(currAddr->pfn == 0){
			printf("Page not present");
		}else if(currAddr->pfn == ULONG_MAX){
			printf("Page swapped");
		}else{
			printf(
				"0x%06llx %1d %s 0x%08llx",
				currAddr->pfn, 
				currAddr->zoneId, currAddr->name,
				currAddr->flags
			);
		}
		printf("\n");
	}
}
