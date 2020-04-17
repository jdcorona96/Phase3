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
int rc;
int i;
int pageSize;
int freeFramesSid;
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
	
	// update the page table in the MMU (USLOSS_MmuSetPageTable)
	int numPages;
	void *VMaddress;
	VMaddress = USLOSS_MmuRegion(&numPages);

	rc = USLOSS_MmuSetPageTable(pageTable);
	assert(rc == USLOSS_MMU_OK);


	//TODO:
	// do we need to apply semaphore? No becuase its chchecked in pager and pager calls this?
	// do we need to adjust free frame array?
	// probably didn't set page table correctly, didn't return pointer
    
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
	
	// update the page table in the MMU (USLOSS_MmuSetPageTable)
	int numPages;
	void *VMaddress;
	VMaddress = USLOSS_MmuRegion(&numPages);

	rc = USLOSS_MmuSetPageTable(pageTable);
	assert(rc == USLOSS_MMU_OK);

    return P1_SUCCESS;

	//TODO: FIX MMU PAGE TABLE
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

    fault.offset = (int) arg;
    // fill in other fields in fault
    // add to queue of pending faults
    // let pagers know there is a pending fault
    // wait for fault to be handled

    // kill off faulting process so skeleton code doesn't hang
    // delete this in your solution
    P2_Terminate(42);
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
    int     result = P1_SUCCESS;

    USLOSS_IntVec[USLOSS_MMU_INT] = FaultHandler;

    // initialize the pager data structures
    // fork off the pagers and wait for them to start running

    return result;
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
    int result = P1_SUCCESS;

    // cause the pagers to quit
    // clean up the pager data structures

    return result;
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
