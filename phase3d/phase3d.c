/*
 * phase3d.c
 *
 */

/***************

NOTES ON SYNCHRONIZATION

There are various shared resources that require proper synchronization. 

Swap space. Free swap space is a shared resource, we don't want multiple pagers choosing the
same free space to hold a page. You'll need a mutex around the free swap space.

The clock hand is also a shared resource.

The frames are a shared resource in that we don't want multiple pagers to choose the same frame via
the clock algorithm. That's the purpose of marking a frame as "busy" in the pseudo-code below. 
Pagers ignore busy frames when running the clock algorithm.

A process's page table is a shared resource with the pager. The process changes its page table
when it quits, and a pager changes the page table when it selects one of the process's pages
in the clock algorithm. 

Normally the pagers would perform I/O concurrently, which means they would release the mutex
while performing disk I/O. I made it simpler by having the pagers hold the mutex while they perform
disk I/O.

***************/


#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <string.h>
#include <libuser.h>

#include "phase3.h"
#include "phase3Int.h"

#ifdef DEBUG
static int debugging3 = 1;
#else
static int debugging3 = 0;
#endif

static void debug3(char *fmt, ...)
{
    va_list ap;

    if (debugging3) {
        va_start(ap, fmt);
        USLOSS_VConsole(fmt, ap);
    }
}

///////////// Data structures ///////////////////////////

//swap space data structure & sempaphore
//clock hand sempahore
//frame busy flag
//pager's page table semaphore

typedef struct SwapSpace {
    
    int pid;
    int page;
    int allocated;

} SwapSpace;

int swapTableSem;

SwapSpace *swapTable;

int clockHand;
int hand = -1;

typedef struct Frame {
    
    int inDisk;
    int track;
    int sector;
    int free;
    int busy;

} Frame;

Frame *frameTable;

int pageTableSem;

int initialized = 0;
int rc;
int i;

int sectorByte;
int sectorNum;
int trackNum;

int pagesNum;
int framesNum;
/////////////////////////////////////////////////////////

/*
 *----------------------------------------------------------------------
 *
 * P3SwapInit --
 *
 *  Initializes the swap data structures.
 *
 * Results:
 *   P3_ALREADY_INITIALIZED:    this function has already been called
 *   P1_SUCCESS:                success
 *
 *----------------------------------------------------------------------
 */
int
P3SwapInit(int pages, int frames)
{

    if (initialized)
        return P3_ALREADY_INITIALIZED;

    initialized = 1;

    pagesNum = pages;
    framesNum = frames;

    // initialize the swap data structures, e.g. the pool of free blocks
    rc = P2_DiskSize(P3_SWAP_DISK, &sectorByte, &sectorNum, &trackNum);
    assert(rc == P1_SUCCESS);

    swapTable = (SwapSpace*) malloc(sizeof(SwapSpace) * sectorNum * trackNum);
    for (i = 0; i < sectorNum * trackNum; i++) {
        swapTable[i].pid = -1;
        swapTable[i].page = -1;
        swapTable[i].allocated = 0;
    }

    rc = P1_SemCreate("Swap Table", 1, &swapTableSem);
    
    rc = P1_SemCreate("Clock Hand", 1, &clockHand);
    assert(rc == P1_SUCCESS);

    frameTable = (Frame*) malloc(sizeof(Frame) * frames);

    rc = P1_SemCreate("Page Table", 1, &pageTableSem);

    return P1_SUCCESS;
}
/*
 *----------------------------------------------------------------------
 *
 * P3SwapShutdown --
 *
 *  Cleans up the swap data structures.
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3SwapInit has not been called
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */
int
P3SwapShutdown(void)
{
    if (!initialized)
        return P3_NOT_INITIALIZED;

    free(swapTable);
    
    rc = P1_SemFree(swapTableSem);
    assert(rc == P1_SUCCESS);

    rc = P1_SemFree(clockHand);
    assert(rc == P1_SUCCESS);

    free(frameTable);

    rc = P1_SemFree(pageTableSem);
    assert(rc == P1_SUCCESS);

    // clean things up

    return P1_SUCCESS;
}

/*
 *----------------------------------------------------------------------
 *
 * P3SwapFreeAll --
 *
 *  Frees all swap space used by a process
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3SwapInit has not been called
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */

int
P3SwapFreeAll(int pid)
{

    if (!initialized)
        return P3_NOT_INITIALIZED;

    if (pid < 0 || pid >= P1_MAXPROC)
        return P1_INVALID_PID;

    //P(mutex)
    rc = P1_P(swapTableSem);
    assert(rc == P1_SUCCESS);
    
    //free all swap space used by the process
    for (i = 0; i < sectorNum * trackNum ;i++) {
        if (swapTable[i].pid == pid) {
            
            swapTable[i].pid  = -1;
            swapTable[i].page = -1;
            swapTable[i].allocated = -1;
        }
    }
    
    //V(mutex)
    rc = P1_V(swapTableSem);
    assert(rc == P1_SUCCESS);


    return P1_SUCCESS;
}

/*
 *----------------------------------------------------------------------
 *
 * P3SwapOut --
 *
 * Uses the clock algorithm to select a frame to replace, writing the page that is in the frame out 
 * to swap if it is dirty. The page table of the page’s process is modified so that the page no 
 * longer maps to the frame. The frame that was selected is returned in *frame. 
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3SwapInit has not been called
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */
int
P3SwapOut(int *frame) 
{

    int target;
    int access;
    if (!initialized)
        return P3_NOT_INITIALIZED;


    rc = P1_P(clockHand);
    assert(rc == P1_SUCCESS);

    while(1) {
        
        hand = (hand +1) % P3_vmStats.frames;
        if (!frameTable[hand].busy) {
            frameTable[hand].busy = 1;
            rc = USLOSS_MmuGetAccess(hand,&access);
            assert(rc == USLOSS_MMU_OK);
            if (access != USLOSS_MMU_REF) {
                target = hand;
                break;
            } else {
                rc = USLOSS_MmuSetAccess(hand, USLOSS_MMU_DIRTY);
                assert(rc == USLOSS_MMU_OK);
            }
        }
    } // while

    rc = USLOSS_MmuGetAccess(target,&access);
    assert(rc == USLOSS_MMU_OK);
    if (access == USLOSS_MMU_DIRTY) {

        void *addr;
        rc = P3FrameMap(target,&addr);
        assert(rc == P1_SUCCESS);

        // TODO: will need to check if right
        for (i = 0; i < sectorNum * trackNum ;i++) {
            if (swapTable[i].pid == -1) {
                
                int process = P1_GetPid();
                swapTable[i].pid = process;
                // TODO office hours waiting asnwer
                // swaptTable[i].page = page???
                
                swapTable[i].allocated = 1;
            }
        }
        rc = P2_DiskWrite(P3_SWAP_DISK, i / trackNum, i % sectorNum, 1, addr);
        assert(rc == P1_SUCCESS);

        rc = P3FrameUnmap(target);
        assert(rc == P1_SUCCESS);

        rc = USLOSS_MmuSetAccess(target, USLOSS_MMU_DIRTY);
        assert(rc == USLOSS_MMU_OK);

    }


    int process = P1_GetPid();
    USLOSS_PTE *pageTable;
    rc = P3PageTableGet(process, &pageTable);
    assert(rc == P1_SUCCESS);

    for (i = 0; i < pagesNum; i++) {
        if (pageTable[i].frame == target) {

            pageTable[i].incore = 0;
            // pageTable[i].frame = -1;
            break;
        }
    }

    rc = P3PageTableSet(process, pageTable);
    assert(rc == P1_SUCCESS);

    frameTable[target].busy = 1;
    rc = P1_V(clockHand);
    assert(rc == P1_SUCCESS);
    *frame = target;


    /*****************

    NOTE: in the pseudo-code below I used the notation frames[x] to indicate frame x. You 
    may or may not have an actual array with this name. As with all my pseudo-code feel free
    to ignore it.

    static int hand = -1;    // start with frame 0
    P(mutex)
    loop
     
    hand = (hand + 1) % # of frames
        if frames[hand] is not busy
            if frames[hand] hasn't been referenced (USLOSS_MmuGetAccess)
                target = hand
                break
            else
                clear reference bit (USLOSS_MmuSetAccess)
    if frame[target] is dirty (USLOSS_MmuGetAccess)
        write page to its location on the swap disk (P3FrameMap,P2_DiskWrite,P3FrameUnmap)
        clear dirty bit (USLOSS_MmuSetAccess)
    update page table of process to indicate page is no longer in a frame
    mark frames[target] as busy
    V(mutex)
    *frame = target

    *****************/

    

    return P1_SUCCESS;
}
/*
 *----------------------------------------------------------------------
 *
 * P3SwapIn --
 *
 *  Opposite of P3FrameMap. The frame is unmapped.
 *
 * Results:
 *   P3_NOT_INITIALIZED:     P3SwapInit has not been called
 *   P1_INVALID_PID:         pid is invalid      
 *   P1_INVALID_PAGE:        page is invalid         
 *   P1_INVALID_FRAME:       frame is invalid
 *   P3_EMPTY_PAGE:          page is not in swap
 *   P1_OUT_OF_SWAP:         there is no more swap space
 *   P1_SUCCESS:             success
 *
 *----------------------------------------------------------------------
 */
int
P3SwapIn(int pid, int page, int frame)
{

    int result = P1_SUCCESS;

    if (!initialized)
        return P3_NOT_INITIALIZED;

    if (pid < 0 || pid >= P1_MAXPROC)
        return P1_INVALID_PID;

    if (page < 0 || page >= pagesNum)
        return P3_INVALID_PAGE;

    if (frame < 0 || frame >= framesNum)
        return P3_INVALID_FRAME;


    rc = P1_P(swapTableSem);
    assert(rc == P1_SUCCESS);

    int flag = 0;

    for (i = 0; i < trackNum * sectorNum; i++) {
        if (swapTable[i].pid == pid &&
                swapTable[i].page == page) {
            

            void* addr;
            rc = P3FrameMap(frame,&addr);
            assert(rc == P1_SUCCESS);

            rc = P2_DiskRead(P3_SWAP_DISK, i / trackNum, i % sectorNum, 1, addr);
            assert(rc == P1_SUCCESS);

            rc = P3FrameUnmap(frame);
            assert(rc == P1_SUCCESS);
            flag = 1;
            break;
        }
    }

    if (!flag) {

        // TODO: how to allocate?
        for (i = 0 ; i < trackNum * sectorNum; i++) {
            if (swapTable[i].pid == -1) {
                
                swapTable[i].pid = pid;
                swapTable[i].page = page;
                swapTable[i].allocated = 0;
                result =  P3_EMPTY_PAGE;
            }

        }

        result = P3_OUT_OF_SWAP;

    }

    frameTable[frame].busy = 0;
    
    rc = P1_V(swapTableSem);
    assert(rc == P1_SUCCESS);


    /*****************

    P(mutex)
    if page is on swap disk
        read page from swap disk into frame (P3FrameMap,P2_DiskRead,P3FrameUnmap)
    else
        allocate space for the page on the swap disk
        if no more space
            result = P3_OUT_OF_SWAP
        else
            result = P3_EMPTY_PAGE
    mark frame as not busy
    V(mutex)

    *****************/

    return result;
}
