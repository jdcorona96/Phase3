/*
 * File: phase3b.c
 * 
 * Authors: 
 *		Joseph Corona | jdcorona96
 * 		Luke Cernetic | lacernetic
 *
 */

#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <string.h>
#include <libuser.h>

#include "phase3Int.h"

void
P3PageFaultHandler(int type, void *arg)
{
	int rc;
	int pid = P1_GetPid();
	int i;
	USLOSS_PTE *table;

	
	rc = USLOSS_MmuGetCause();
	if (rc == USLOSS_MMU_FAULT){

		rc = P3PageTableGet(pid, &table);

		if (table == NULL) {
			USLOSS_Console("ERROR: The current process does not have a page table.");
			USLOSS_Halt(1);
		}
		else {
			
			// determine number of pages by dividing table size by page size.
			//int pageSize = USLOSS_MmuPageSize();
			//int numPages = sizeof(table) / pageSize;

			// iterate over all pages until an empty page is found.
			for (i = 0; i < 10000/*numPages*/; i++){
				if (table[i].incore == 0) {
					break;
				}
			}

			// fill in PTE's values
			table[i].read = 1;
			table[i].write = 1;
			table[i].incore = 1;
			table[i].frame = i;

			// set table in MMU
			rc = USLOSS_MmuSetPageTable(table);
		}
	}
	else {
		USLOSS_Console("ERROR: USLOSS_MMU_FAULT not returned.");
		USLOSS_Halt(1);
	}
    /*******************

    if the cause is USLOSS_MMU_FAULT (USLOSS_MmuGetCause)
        if the process does not have a page table  (P3PageTableGet)
            print error message
            USLOSS_Halt(1)
        else
            determine which page suffered the fault (USLOSS_MmuPageSize)
            update the page's PTE to map page x to frame x
            set the PTE to be read-write and incore
            update the page table in the MMU (USLOSS_MmuSetPageTable)
    else
        print error message
        USLOSS_Halt(1)
    *********************/

}

USLOSS_PTE *
P3PageTableAllocateEmpty(int pages)
{
	//TODO: When does this return null?
    USLOSS_PTE  *table = NULL;

	int i;
	// allocates memory for each page
	table = malloc(sizeof(USLOSS_PTE) * pages);
	for (i = 0; i < pages; i++){
		
		// sets each page's initial values
		table[i].incore = 0;
		table[i].read = 1;
		table[i].write = 1;
		table[i].frame = i;
	}
    return table;
}
