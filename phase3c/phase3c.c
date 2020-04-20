/*
 * Authors: (Luke Cernetic | lacernetic) && (Joseph Corona | jdcorona96)
 *
 * File: phase3c.c
 *
 * Purpose:
 */

#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <string.h>
#include <libuser.h>

#include "phase3.h"
#include "phase3Int.h"

#ifdef DEBUG
int debugging3 = 1;
#else
int debugging3 = 0;
#endif

// global variables
int isInit = 0;
int isInitPager = 0;
int rc;
int i;
int pageSize;
int freeFramesSid;
int faultListSid;
int pagersStatSid;
FaultList *head;

int numPagers = 0;
int pagerPids[P3_MAX_PAGERS];

int faults = 0;


//

void debug3(char *fmt, ...)
{
    va_list ap;

    if (debugging3) {
        va_start(ap, fmt);
        USLOSS_VConsole(fmt, ap);
    }
}

void kernelMode(void){
	int psr = USLOSS_PsrGet();

	if (!(psr & USLOSS_PSR_CURRENT_MODE)) {
		USLOSS_IllegalInstruciton();
	}
}

// This allows the skeleton code to compile. Remove it in your solution.

/*
 *----------------------------------------------------------------------
 *
 * P3FrameInit --
 *
 *  Initializes the frame data structures.
 *
 * Results:
 *   P3_ALREADY_INITIALIZED:    this function has already been called
 *   P1_SUCCESS:                success
 *
 *----------------------------------------------------------------------
 */

struct Frame {
	int pid;
	int page;
};

struct Frame *frameTable;

int
P3FrameInit(int pages, int frames)
{
	kernelMode();
	
	pageSize = pages;

	if (isInit == 1) {
		return P3_ALREADY_INITIALIZED;
	}

	// initialize the frame data structures, e.g. the pool of free frames
	frameTable = malloc(sizeof(struct Frame) *frames);
	for (i = 0; i < frames; i++){
		frameTable[i].pid = -1;
		frameTable[i].page = -1;
	}
	
	// creates a semaphore for freeFrames so that two pagers cannot access it at the same time.
	rc = P1_SemCreate("freeFrames", 0, &freeFramesSid);
	assert(rc == P1_SUCCESS);

	// sets values of P3_VmStats
	P3_VmStats.frames = frames;
	P3_VmStats.freeFrames = frames;
    
    return P1_SUCCESS;
}
/*
 *----------------------------------------------------------------------
 *
 * P3FrameShutdown --
 *
 *  Cleans up the frame data structures.
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3FrameInit has not been called
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */
int
P3FrameShutdown(void)
{
	kernelMode();

	if (isInit == 0) {
		return P3_NOT_INITIALIZED;
	}

	free(frameTable);
	frameTable = NULL;

	rc = P1_SemFree(freeFramesSid);
	assert(rc == P1_SUCCESS);

    return P1_SUCCESS;
}

/*
 *----------------------------------------------------------------------
 *
 * P3FrameFreeAll --
 *
 *  Frees all frames used by a process
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3FrameInit has not been called
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */

int
P3FrameFreeAll(int pid)
{
	kernelMode();

	// get the page table
	PTE **pageTable;
	rc = P3PageTableGet(pid, pageTable);
	assert(rc == P1_SUCCESS);

	i = 0;

	// iterate over all pages in the page table
	for (i = 0; i < pageSize; i++){
		
		// if the page has a frame, remove the frame and free it in frameTable
		if(pageTable[i].incore == 1) {
			pageTable[i].incore = 0;
			frameTable[pageTable[i].frame].pid = -1;
			frameTable[pageTable[i].frame].page = -1;
		}
	}

    return P1_SUCCESS;
}

/*
 *----------------------------------------------------------------------
 *
 * P3FrameMap --
 *
 *  Maps a frame to an unused page and returns a pointer to it.
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3FrameInit has not been called
 *   P3_OUT_OF_PAGES:       process has no free pages
 *   P1_INVALID_FRAME       the frame number is invalid
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */
int
P3FrameMap(int frame, void **ptr) 
{
	kernelMode();
	if (isInit == 0){
		return P3_NOT_INITIALIZED;
	}

	if (frame < 0 || frame > P3_VmStats.frames) {
		return P1_INVALID_FRAME;
	}

	// get the page table for the process (P3PageTableGet)
	PTE **pageTable;
	rc = P3PageTableGet(pid, pageTable);
	assert(rc == P1_SUCCESS);

	int flag = 0;

	for (i = 0; i < pageSize; i++) {
		
		// update the page's PTE to map the page to the frame
		if (pageTable[i].incore == 0) {
			flag = 1;
			pageTable[i].incore = 1;
			pageTable[i].frame = frame;
			break;
		}
	}
	
	if (flag == 0){
		return P3_OUT_OF_PAGES;
	}
	
	rc = USLOSS_MmuSetPageTable(pageTable);
	assert(rc == USLOSS_MMU_OK);

	// update the page table in the MMU (USLOSS_MmuSetPageTable)
	int numPages;
	void *VMaddress;
	VMaddress = USLOSS_MmuRegion(&numPages);
	ptr = VMaddress[sizeof(struct USLOSS_PTE) * i];

    return P1_SUCCESS;
}
/*
 *----------------------------------------------------------------------
 *
 * P3FrameUnmap --
 *
 *  Opposite of P3FrameMap. The frame is unmapped.
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3FrameInit has not been called
 *   P3_FRAME_NOT_MAPPED:   process didn’t map frame via P3FrameMap
 *   P1_INVALID_FRAME       the frame number is invalid
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */
int
P3FrameUnmap(int frame) 
{
	kernelMode();
	if (isInit == 0){
		return P3_NOT_INITIALIZED;
	}
	isInit = 1;

	if (frame < 0 || frame > P3_VmStats.frames) {
		return P1_INVALID_FRAME;
	}

	// get the page table for the process (P3PageTableGet)
	PTE **pageTable;
	rc = P3PageTableGet(pid, pageTable);
	assert(rc == P1_SUCCESS);

	int flag = 0;

	for (i = 0; i < pageSize; i++) {
		
		// update the page's PTE to unmap the page to the frame
		if (pageTable[i].incore == 1 && pageTable[i].frame = frame) {
			flag = 1;
			pageTable[i].incore = 0;
			pageTable[i].frame = NULL;
			break;
		}
	}
	
	if (flag == 0){
		return P3_FRAME_NOT_MAPPED;
	}

	//update page table in MMU
	rc = USLOSS_MmuSetPageTable(pageTable);
	assert(rc == USLOSS_MMU_OK);

    return P1_SUCCESS;
}

// information about a fault. Add to this as necessary.

typedef struct Fault {
    PID         pid;
    int         offset;
    int         cause;
    SID         wait;
    // other stuff goes here
} Fault;


/*
 *----------------------------------------------------------------------
 *
 * FaultHandler --
 *
 *  Page fault interrupt handler
 *
 *----------------------------------------------------------------------
 */

static void
FaultHandler(int type, void *arg)
{
    Fault   fault UNUSED;
    
	// fill in other fields in fault
    fault.offset = (int) arg;
	fault.pid = P1_GetPid();
	fault.cause = USLOSS_MmuGetCause();
	fault.wait = faultListSid; 

	FaultList newFault = malloc(sizeof(FaultList));
	newFault.fault = fault;
	
    // add to queue of pending faults
	P1_P(faultListSid);
	if (head == NULL){
		head = newFault;
	}
	else {
		FaultList curr = head;
		while (curr->next != NULL){
			curr = curr->next;		
		}
		curr->next = newFault;
	}
	P1_V(faultListSid);

    // let pagers know there is a pending fault
	faults++;
	
	// wait for fault to be handled

}

struct FaultList {
	Fault fault;
	FaultList *next;
};

/*
 *----------------------------------------------------------------------
 *
 * P3PagerInit --
 *
 *  Initializes the pagers.
 *
 * Results:
 *   P3_ALREADY_INITIALIZED: this function has already been called
 *   P3_INVALID_NUM_PAGERS:  the number of pagers is invalid
 *   P1_SUCCESS:             success
 *
 *----------------------------------------------------------------------
 */
int
P3PagerInit(int pages, int frames, int pagers)
{
	if (isInitPager == 1){
		return P3_ALREADY_INITIALIZED;
	}
	isInitPager = 1;

	if (pagers < 1 || pagers > P3_MAX_PAGERS){
		return P3_INVALID_NUM_PAGERS;
	}
	
	// initialize the pager data structure
	head = NULL;

    USLOSS_IntVec[USLOSS_MMU_INT] = FaultHandler;

	// creates semaphore for faultList
	rc = P1_SemCreate("faultList", 1, &faultListSid);
	assert(rc == P1_SUCCESS);

	// creates semaphore for pagerStatsSid
	rc = P1_SemCreate("pagerStats", 1, &pagerStatsSid);
	assert(rc == P1_SUCCESS);

	// forks off the pagers
	for (i = 0; i < pagers; i++){
		numPagers++;
		char *name = (char*) malloc(sizeof(char) * 7);
		sprintf(name, "pager%d\0", i);
		P1_Fork(name, static int (*Pager)(void *), NULL, USLOSS_MIN_STACK, P3_PAGER_PRIORITY, 0, pagerPids + i);
	}

    return P1_SUCCESS;
}

/*
 *----------------------------------------------------------------------
 *
 * P3PagerShutdown --
 *
 *  Kills the pagers and cleans up.
 *
 * Results:
 *   P3_NOT_INITIALIZED:     P3PagerInit has not been called
 *   P1_SUCCESS:             success
 *
 *----------------------------------------------------------------------
 */
int
P3PagerShutdown(void)
{
	if (isInitPager == 0){
		return P3_NOT_INITIALIZED;
	}
	
    // clean up the pager data structures
	FaultList curr = head;
	while (curr != NULL){
		FaultList next = curr->next;
		free(curr);
		curr = next;
	}

	//TODO:
    // cause the pagers to quit
	for (i = 0; i < numPagers; i++){

	}

	// free the semaphores created in PagerInit
	rc = P1_SemFree(faultListSid);
	assert(rc == P1_SUCCESS);

	rc = P1_SemFree(pagersStatSid);
	assert(rc == P1_SUCCESS);



    return P1_SUCCESS;
}

/*
 *----------------------------------------------------------------------
 *
 * Pager --
 *
 *  Handles page faults
 *
 *----------------------------------------------------------------------
 */

static int
Pager(void *arg)
{
	




    /********************************

    notify P3PagerInit that we are running
    loop until P3PagerShutdown is called
        wait for a fault
        if it's an access fault kill the faulting process
        if there are free frames
            frame = a free frame
        else
            P3SwapOut(&frame);
        rc = P3SwapIn(pid, page, frame)
        if rc == P3_EMPTY_PAGE
            P3FrameMap(frame, &addr)
            zero-out frame at addr
            P3FrameUnmap(frame);
        else if rc == P3_OUT_OF_SWAP
            kill the faulting process
        update PTE in faulting process's page table to map page to frame
        unblock faulting process

    **********************************/

    return 0;
}
