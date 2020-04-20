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
int pagersStatsSid;
int	emptyFaultSid;
struct FaultList *head;


int numPagers = 0;
int pagerPids[P3_MAX_PAGERS];



//

static int Pager(void *ptr);

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
		USLOSS_IllegalInstruction();
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
	P3_vmStats.frames = frames;
	P3_vmStats.freeFrames = frames;
    
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
	USLOSS_PTE **pageTable = NULL;
	rc = P3PageTableGet(pid, pageTable);
	assert(rc == P1_SUCCESS);

	i = 0;

	// iterate over all pages in the page table
	for (i = 0; i < pageSize; i++){
		
		// if the page has a frame, remove the frame and free it in frameTable
		if(pageTable[i]->incore == 1) {
			pageTable[i]->incore = 0;
			frameTable[pageTable[i]->frame].pid = -1;
			frameTable[pageTable[i]->frame].page = -1;
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

	if (frame < 0 || frame > P3_vmStats.frames) {
		return P3_INVALID_FRAME;
	}

	// get the page table for the process (P3PageTableGet)
	USLOSS_PTE **pageTable = NULL;
	int pid = P1_GetPid();
	rc = P3PageTableGet(pid, pageTable);
	assert(rc == P1_SUCCESS);

	int flag = 0;

	for (i = 0; i < pageSize; i++) {
		
		// update the page's PTE to map the page to the frame
		if (pageTable[i]->incore == 0) {
			flag = 1;
			pageTable[i]->incore = 1;
			pageTable[i]->frame = frame;
			break;
		}
	}
	
	if (flag == 0){
		return P3_OUT_OF_PAGES;
	}
	
	rc = USLOSS_MmuSetPageTable(*pageTable);
	assert(rc == USLOSS_MMU_OK);

	// update the page table in the MMU (USLOSS_MmuSetPageTable)
	int numPages;
	void *VMaddress;
	VMaddress = USLOSS_MmuRegion(&numPages);

	void **pointer = malloc(sizeof(void **));
	*pointer = (VMaddress + (sizeof(USLOSS_PTE) * i));
	ptr = pointer;
//	ptr = &(VMaddress + (sizeof(USLOSS_PTE) * i));

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

	if (frame < 0 || frame > P3_vmStats.frames) {
		return P3_INVALID_FRAME;
	}

	// get the page table for the process (P3PageTableGet)
	USLOSS_PTE **pageTable = NULL;

	int pid = P1_GetPid();
	rc = P3PageTableGet(pid, pageTable);
	assert(rc == P1_SUCCESS);

	int flag = 0;

	for (i = 0; i < pageSize; i++) {
		
		// update the page's PTE to unmap the page to the frame
		if (pageTable[i]->incore == 1 && pageTable[i]->frame == frame) {
			flag = 1;
			pageTable[i]->incore = 0;
			pageTable[i]->frame = -1;
			break;
		}
	}
	
	if (flag == 0){
		return P3_FRAME_NOT_MAPPED;
	}

	//update page table in MMU
	rc = USLOSS_MmuSetPageTable(*pageTable);
	assert(rc == USLOSS_MMU_OK);

    return P1_SUCCESS;
}

// information about a fault. Add to this as necessary.

typedef struct Fault {
    PID         pid;
    int         offset;
    int         cause;
    SID         wait;
	int 		outOfSwap;
    // other stuff goes here
} Fault;

struct FaultList {
	Fault fault;
	struct FaultList *next;
};

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
	kernelMode();
    Fault fault;
    
	// fill in other fields in fault
    fault.offset = (int) arg;
	fault.pid = P1_GetPid();
	fault.cause = USLOSS_MmuGetCause();
	fault.wait = faultListSid; 
	fault.outOfSwap = 0;

	struct FaultList *newFault = (struct FaultList*) malloc(sizeof(struct FaultList));
	newFault->fault = fault;
	
    // add to queue of pending faults
	rc = P1_P(faultListSid);
	assert(rc == P1_SUCCESS);

	if (head == NULL){
		head = newFault;
	}
	else {
		struct FaultList *curr = head;
		while (curr->next != NULL){
			curr = curr->next;		
		}
		curr->next = newFault;
	}
	rc = P1_V(faultListSid);
	assert(rc == P1_SUCCESS);
    // let pagers know there is a pending fault and wait
	rc = P1_V(emptyFaultSid);
	assert(rc == P1_SUCCESS);
	
	//TODO fix the syntax
	if (fault.cause == USLOSS_MMU_ACCESS){
		P2_Terminate(USLOSS_MMU_ACCESS);
	}
	if (fault.outOfSwap == 1){
		P2_Terminate(P3_OUT_OF_SWAP);
	}
}



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
	kernelMode();

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
	rc = P1_SemCreate("pagerStats", 1, &pagersStatsSid);
	assert(rc == P1_SUCCESS);

	// creates semaphore to notify when a process has faulted
	rc = P1_SemCreate("emptyFault", 0, &emptyFaultSid);
	assert(rc == P1_SUCCESS);

	// forks off the pagers
	for (i = 0; i < pagers; i++){
		numPagers++;
		char *name = (char*) malloc(sizeof(char) * 7);
		sprintf(name, "pager%d", i);
		rc = P1_Fork(name, Pager, NULL, USLOSS_MIN_STACK, P3_PAGER_PRIORITY, 0, pagerPids + i);
		assert(rc == P1_SUCCESS);
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
	kernelMode();
	if (isInitPager == 0){
		return P3_NOT_INITIALIZED;
	}
	
    // clean up the pager data structures
	struct FaultList *curr = head;
	while (curr != NULL){
		struct FaultList *next = curr->next;
		free(curr);
		curr = next;
	}

	//TODO:
    // cause the pagers to quit
	numPagers = 0;

	// free the semaphores created in PagerInit
	rc = P1_SemFree(faultListSid);
	assert(rc == P1_SUCCESS);

	rc = P1_SemFree(pagersStatsSid);
	assert(rc == P1_SUCCESS);

	rc = P1_SemFree(emptyFaultSid);
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
	kernelMode();

	// notify P3PagerInit that we are running
	while (numPagers){
		
		// will pause here until there exists a fault
		rc = P1_P(emptyFaultSid);
		assert(rc == P1_SUCCESS);
		
		// locks fault list
		rc = P1_P(faultListSid);
		assert(rc == P1_SUCCESS);

		// grabs first fault
		struct Fault currFault = head->fault;
		head = head->next;

		
		// unlocks fault list
		rc = P1_V(faultListSid);
		assert(rc == P1_SUCCESS);
		
		if (currFault.cause == USLOSS_MMU_ACCESS) {
			continue;	
		}


		//TODO: semaphores for freeframe and maybe more?
		int currFrame;
		if (P3_vmStats.freeFrames > 0){
			for (i = 0; i < P3_vmStats.frames; i++){
				if (frameTable[i].pid == -1){
					currFrame = i;
				}
			}
		}
		else {
			rc = P3SwapOut(&currFrame);
			assert(rc == P1_SUCCESS);
		}
		
		rc = P3SwapIn(currFault.pid, currFault.offset, currFrame);

		struct USLOSS_PTE **addr = NULL;
		if (rc == P3_EMPTY_PAGE){
			rc = P3FrameMap(currFrame, (void**) addr);
			assert(rc == P1_SUCCESS);

			//zero-out frame at addr
			(*addr)->incore = 0;
			(*addr)->read = 0;
			(*addr)->write = 0;
			(*addr)->frame = 0;
			rc = P3FrameUnmap(currFrame);
			assert(rc == P1_SUCCESS);
		}
		else if (rc == P3_OUT_OF_SWAP){
			
			//kill the faulting process
			currFault.outOfSwap = 1; 
			continue;
		}

		//update PTE in faulting process's page table to map page to frame
		(*addr)->incore = 1;
		(*addr)->read = 1;
		(*addr)->write = 1;
		(*addr)->frame = currFrame;

		// unblock faulting process

	}






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
