#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <current.h>
#include <machine/tlb.h>
#include <synch.h>
#include <proc.h>
#include <spl.h>
#include <../../userland/include/unistd.h>

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// VIRTUAL MEMORY SPECEFIC FUNCTIONS (VM_*) ////////////////////
/////////////////////////////////////////////////////////////////////////////////////////

// Major vm_fault handler deals with the TLB_miss entries and lookups for the 
// Entries associated with the fault address.
int vm_fault(int faulttype, vaddr_t faultaddress) {
    
    // Handle the associated errors in the beginning
    int err_vm_fault = err_handling_vm_fault(faultaddress);
   
    if (err_vm_fault) {
        return err_vm_fault;
    }

    struct addrspace *as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

    Region_t Valid_Region = Lookup_Region(as, faultaddress);
    
    HeapRegion_t Valid_Heap = Lookup_Heap(as, faultaddress);

    Mmap_Region_t Valid_File = Lookup_Mmap(as, faultaddress);

    if (Valid_Region == NULL && Valid_Heap == NULL && Valid_File == NULL) {
        return EFAULT;
    }

    int miss_tlb = SUCCESS;

    // If we get VM_FAULT_READONLY retrun EFUALT otherwise deal with the
    // tlb_miss_hanlder
    switch (faulttype) {
        
        case VM_FAULT_READONLY:
                
                if (Valid_Region->is_readonly == true) {
                    return EFAULT; 
                }
                
                else {
                    int err_copy_on_write = copy_on_write(as, faultaddress);

                    if (err_copy_on_write) {
                        return err_copy_on_write;
                    }
                }
            
            
        break;   

        case VM_FAULT_READ:
	    
        case VM_FAULT_WRITE:
            
            break;
            
    }

    miss_tlb = tlb_miss_handler(faultaddress, as, Valid_Region, Valid_Heap, Valid_File);
    
    return miss_tlb;

}

/*
 * SMP-specific functions.  Unused in our UNSW configuration.
 */

void vm_tlbshootdown(const struct tlbshootdown *ts) {
	
    (void)ts;
	panic("vm tried to do tlb shootdown?!\n");

}

void vm_bootstrap(void) {
    /* Initialise any global components of your VM sub-system here.  
     *  
     * You may or may not need to add anything here depending what's
     * provided or required by the assignment spec.
     */
}


////////////////////////////////////////////////////////////////////////////////////////
/////////////////////// VM_FAULT ERROR HANDLING AND HELPER FUNCTIONS ///////////////////
////////////////////////////////////////////////////////////////////////////////////////

int err_handling_vm_fault(vaddr_t faultaddress) {

    //taken from dumbvm.c
    if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

    if (faultaddress > MIPS_KSEG0) {
        return EFAULT;
    }

    return SUCCESS;
}

int copy_on_write(struct addrspace *as, vaddr_t faultaddress) {

    paddr_t prev_frame_addr = Page_table_lookup(as, faultaddress);
    
    if (prev_frame_addr  == 0) {
        return EINVAL;
    }

    paddr_t prev_frame_no = prev_frame_addr & PAGE_FRAME;
    int ref_count = frame_ref_count_check(prev_frame_no);

    // Finding the level 3 index by the 6 bits just after
    // the offset(using approporiate mask 0x3F = (111111))
    faultaddress = faultaddress >> 12;
    uint32_t TLI = faultaddress & 0x3F;
    
    // Finding the level 2 index by the 6 bits just after
    // the Third level 6 bits(using approporiate mask 0x3F = (111111))
    faultaddress = faultaddress >> 6;
    uint32_t SLI = faultaddress & 0x3F;
    
    // Finding the level 1 index by the 8 bits just after
    // the Third level 6 bits(using the appropriate maks 0xFF(11111111))
    faultaddress = faultaddress >> 6;
    uint32_t FLI = faultaddress & 0xFF;

    
    if (ref_count == 1) {

        as->PageTable->Pages[FLI][SLI][TLI] = prev_frame_no | TLBLO_DIRTY | TLBLO_VALID;
       
    }

    else {

        // Allocate a new frame entry for the curproc.
        vaddr_t new_frame = alloc_kpages(1);
            
        if (new_frame == 0) {
            return ENOMEM;
        }

        // get the physical address and the frame number from it
        paddr_t new_physical_address = KVADDR_TO_PADDR(new_frame);
        paddr_t new_frame_number = new_physical_address & PAGE_FRAME;

        as_zero_region(new_frame, 1);
        
        if (memmove((void *)new_frame, (const void *) 
        PADDR_TO_KVADDR(prev_frame_no), PAGE_SIZE) == NULL) {
            free_kpages(new_frame);
            return ENOMEM;
        }

        // decrease the ref count for the prev one..!!
        free_kpages(PADDR_TO_KVADDR(prev_frame_no));
        
        //Page_table_update(as, faultaddress, ref_count, new_frame_number, dirty_bit, valid_bit);

        // NOTE : Need to check whether this will work or not ?? Can we directly copy the D-V bits from previous one or
        // we need to make it READ_WRITE from the beginning.
        as->PageTable->Pages[FLI][SLI][TLI] = new_frame_number | TLBLO_DIRTY | TLBLO_VALID;
        

    }
    
    as_activate();
    return SUCCESS;

}

// Based on the ASST3 Overview video slides 
// page-27(link-http://cgi.cse.unsw.edu.au/
// ~cs3231/21T1/lectures/asst3.pdf )

int tlb_miss_handler(vaddr_t faultaddress, struct addrspace* as, Region_t Valid_Region, HeapRegion_t Valid_Heap, Mmap_Region_t Valid_File) {

    // Find if there is an associated entry in the memory, 
    // if yes the get the value of entry_lo.
    paddr_t entry_lo = Page_table_lookup(as, faultaddress);

    // if we get the correct frame number then we load the 
    // TLB entry for it randomly by turning the interrupts off.

    if (entry_lo != 0) {
        
        // Find the page_number from the virtual address
        vaddr_t page_number = faultaddress & TLBHI_VPAGE;

        Load_TLB((uint32_t) page_number, (uint32_t) entry_lo);
    } 

    // otherwise check whether the region is a valid region or not,
    // if it is then allocate the frame for it and zero fill it and update 
    // PTE for it. 
    if (entry_lo == 0) {
        
        // Region_t Valid_Region = Lookup_Region(as, faultaddress);

        // if (Valid_Region == NULL) {
        //     return EFAULT;
        // }
        
        int err_alloc_frame = Alloc_Frame_Insert_PTE(faultaddress, as, Valid_Region, Valid_Heap, Valid_File);

        if (err_alloc_frame) {
            return err_alloc_frame;
        }

    }
    
    return SUCCESS;
}

// Load the TLB values by keeping the interuppts off
void Load_TLB(uint32_t entry_hi, uint32_t entry_lo) {

    int spl = splhigh();
    tlb_random(entry_hi, entry_lo);
    splx(spl);

}

// Look for the correc region where the Faultaddress lies and if it is not 
// present then return NULL
Region_t Lookup_Region(struct addrspace* as, vaddr_t faultaddress) {

    Region_t region_addr_proc = as->addr_region_base;

    while (region_addr_proc != NULL) {
        if (faultaddress >= region_addr_proc->base_addr && 
            faultaddress < (region_addr_proc->end_addr) ) {
            return region_addr_proc;
        }
        region_addr_proc = region_addr_proc->next;
    }

    return NULL;

}

HeapRegion_t Lookup_Heap(struct addrspace* as, vaddr_t faultaddress) {

    if (faultaddress >= as->Proc_heap->base_heap_addr && faultaddress < as->Proc_heap->cur_heap_break) {
        return as->Proc_heap;
    }

    return NULL;
}

Mmap_Region_t Lookup_Mmap(struct addrspace *as, vaddr_t faultaddress) {

    Mmap_Region_t File_addr_proc = as->File_region_base;

    while (File_addr_proc != NULL) {
        if (faultaddress < File_addr_proc->Base_address && 
            faultaddress >= (File_addr_proc->Base_address - File_addr_proc->length) ) {
            return File_addr_proc;
        }
        File_addr_proc = File_addr_proc->next;
    }


    return NULL;
}

// Allocates teh frame for the new entry and add the page table entry for the same, and 
// then LOAD the TLB entry for it.
int Alloc_Frame_Insert_PTE(vaddr_t faultaddress, struct addrspace* as, Region_t as_req, HeapRegion_t as_hreq, Mmap_Region_t as_Freq) {
    
    //alocates the physical adddress
    if (as_req != NULL && as_hreq != NULL && as_Freq == NULL) {
        
        vaddr_t allocated_addr = alloc_kpages(1);

        if (allocated_addr == 0) {
            return ENOMEM;
        }

        //Get the associated physical frame number
        paddr_t physical_alloc_addr = KVADDR_TO_PADDR(allocated_addr);
        paddr_t frame_no = physical_alloc_addr & PAGE_FRAME;

        //Nullify the aloocated address
        as_zero_region(allocated_addr, 1);

        int err_add = Page_table_Add(faultaddress, frame_no, as_req, as_hreq, as);

        if (err_add) {
            return err_add;
        }

    }
    
    else {

        vaddr_t allocated_addr = alloc_kpages(as_Freq->Num_pages);

        if (allocated_addr == 0) {
            return ENOMEM;
        }

        //Get the associated physical frame number
        paddr_t physical_alloc_addr = KVADDR_TO_PADDR(allocated_addr);
        paddr_t frame_no = physical_alloc_addr & PAGE_FRAME;

        // Read from the file from the offest of the given length and then add it to the 
        // frame.
        
        off_t moved = lseek(as_Freq->File_descriptor, as_Freq->File_offset, SEEK_SET);

        ssize_t read = read(as_Freq->File_descriptor, void *buf, as_Freq->length);

        if (read == 0) {
            return EFAULT;
        }

        // Add the page table entry associated to the faultaddress
        int err_add = Page_table_Add(faultaddress, frame_no, as_req, as_hreq, as);

        if (err_add) {
            return err_add;
        }
    }

    

    // Get the value of entry_hi and entry_lo value to be loaded in TLB
    // by the addess and the Page table lookup
    uint32_t entry_hi = (uint32_t) (faultaddress & TLBHI_VPAGE);
	uint32_t entry_lo = (uint32_t) Page_table_lookup(as, faultaddress);

    // Load the TLB entry for it
    Load_TLB(entry_hi, entry_lo);

	return SUCCESS;

}


////////////////////////////////////////////////////////////////////////////////////////
////////////////////////// PAGE_TABLE SPECEFIC HANDLING FUNCTIONS //////////////////////
////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////
////////////////////// PAGE_TABLE_SET AND ITS ASSOCIATED HELPER FUNCS. /////////////////
////////////////////////////////////////////////////////////////////////////////////////

Page_table_t Page_table_Set(int *err_PT_set) {

    // Setting up the Pagetable entry by mallocing enough memory for it.
	Page_table_t Proc_PT = kmalloc(sizeof(struct PageTable));

	if (Proc_PT == NULL) {
		*err_PT_set = ENOMEM;
		return NULL;
	}

    // Mallocing memory for the level one entries 
	Proc_PT->Pages = kmalloc(sizeof(paddr_t) * LEVEL1_LIMIT);

	if (Proc_PT->Pages == NULL) {
		*err_PT_set = ENOMEM;
        Page_table_free(Proc_PT);
		return NULL;
	}

    // Level 1 Setup, for the page entries all setup to NULL initially.
    for (int i = 0; i < LEVEL1_LIMIT; i++) {
        Proc_PT->Pages[i] = NULL;
    }

    return Proc_PT;

}

////////////////////////////////////////////////////////////////////////////////////////
////////////////////// PAGE_TABLE_LOOKUP AND ITS ASSOCIATED HELPER FUNCS. //////////////
////////////////////////////////////////////////////////////////////////////////////////

paddr_t Page_table_lookup(struct addrspace* as, vaddr_t faultaddress) {

    // Finding the level 3 index by the 6 bits just after
    // the offset(using approporiate mask 0x3F = (111111))
    faultaddress = faultaddress >> 12;
    uint32_t TLI = faultaddress & 0x3F;
    
    // Finding the level 2 index by the 6 bits just after
    // the Third level 6 bits(using approporiate mask 0x3F = (111111))
    faultaddress = faultaddress >> 6;
    uint32_t SLI = faultaddress & 0x3F;
    
    // Finding the level 1 index by the 8 bits just after
    // the Third level 6 bits(using the appropriate maks 0xFF(11111111))
    faultaddress = faultaddress >> 6;
    uint32_t FLI = faultaddress & 0xFF;
	
    // If no entry for the associated index return 0 
    if (as->PageTable->Pages[FLI] == NULL) {
		return 0;
	}

	else if (as->PageTable->Pages[FLI][SLI] == NULL) {
		return 0;
	}
    
    // otherwise return the value stored in the given index
    paddr_t PT_entry = as->PageTable->Pages[FLI][SLI][TLI];
    
    return PT_entry;

}

////////////////////////////////////////////////////////////////////////////////////////
////////////////////// PAGE_TABLE_INSERT AND ASSOCIATED HELPER FUNCS. //////////////////
////////////////////////////////////////////////////////////////////////////////////////


// Inserts at the level two or three depending on whether the entry assocaited
// with the Indexes already exists or not.
int Page_table_Insert(struct addrspace *as, uint32_t FLI, 
    uint32_t SLI, uint32_t TLI, uint32_t entry_lo) {

    // add both level two and three
    if (as->PageTable->Pages[FLI] == NULL) {
        
		init_level_two (as, FLI);
		init_level_three (as, FLI, SLI);

    } 

    // intialise only level three in this case
    if (as->PageTable->Pages[FLI] != NULL &&
    as->PageTable->Pages[FLI][SLI] == NULL) {
        
        init_level_three (as, FLI, SLI);

    }
    
    // if both are existing then directly set teh entry_lo
    // to the given index.
    as->PageTable->Pages[FLI][SLI][TLI] = entry_lo;

    return SUCCESS;

}

// Mallocs the level two entry and initialise it to 0
int init_level_two (struct addrspace *as, uint32_t FLI) {

    as->PageTable->Pages[FLI] = kmalloc(sizeof(paddr_t) * LEVEL2_AND_3_LIMIT);
        
    if (as->PageTable->Pages[FLI] == NULL) {
        return ENOMEM;
    }
    
    for (int i = 0; i < LEVEL2_AND_3_LIMIT; i++) {
        as->PageTable->Pages[FLI][i] = NULL;
    }

    return SUCCESS;

}

// Mallocs the level three entries and then initialise it to zero.
int init_level_three (struct addrspace *as, uint32_t FLI, uint32_t SLI) {

    as->PageTable->Pages[FLI][SLI] = kmalloc(sizeof(paddr_t) * LEVEL2_AND_3_LIMIT);
        
    if (as->PageTable->Pages[FLI][SLI] == NULL) {
        return ENOMEM;
    }
    
    for (int i = 0; i < LEVEL2_AND_3_LIMIT; i++) {
        as->PageTable->Pages[FLI][SLI][i] = 0;
    }

    return SUCCESS;

}

////////////////////////////////////////////////////////////////////////////////////////
////////////////////// PAGE_TABLE_COPY AND ASSOCIATED HELPER FUNCS. ////////////////////
////////////////////////////////////////////////////////////////////////////////////////

// Mallocs the memory for the level 1 page table and then
int Page_table_copy(Page_table_t oldPT, Page_table_t newPT) {
    
	for (int i = 0 ; i < LEVEL1_LIMIT; i++) {
		
        if (oldPT->Pages[i] != NULL) {
			
            newPT->Pages[i] = kmalloc(sizeof(paddr_t) * LEVEL2_AND_3_LIMIT);
			
            if (newPT->Pages[i] == NULL) {
				return ENOMEM;
			}

            Level_two_copy (oldPT, newPT, i);
        
        }

        else {
			newPT->Pages[i] = NULL;	
		}

    }
	
    return SUCCESS;
}

// Kmallocs the memory for the level two page table entries and then call the
// level 3 allocator/ page table copy function
int Level_two_copy (Page_table_t oldPT, Page_table_t newPT, int i) {
    
    for (int j = 0; j < LEVEL2_AND_3_LIMIT; j++) {
		
        if (oldPT->Pages[i][j] != NULL) {
					
            newPT->Pages[i][j] = kmalloc(sizeof(paddr_t) * LEVEL2_AND_3_LIMIT);
        
            if (newPT->Pages[i][j] == NULL) {
                return ENOMEM;
            }

            Level_three_copy (oldPT, newPT, i, j);	
                
        }

        else {
            newPT->Pages[i][j] = NULL;
        }
    
    }
    
    return SUCCESS;
}

int Level_three_copy (Page_table_t oldPT, Page_table_t newPT, int i, int j) {

    for (int k = 0; k < LEVEL2_AND_3_LIMIT; k++) {
                
        if (oldPT->Pages[i][j][k] != 0) {
            
            // Commented coode was used for the basic assignment..!!
            // alocate new frame
            // vaddr_t new_frame = alloc_kpages(1);
            
            // if (new_frame == 0) {
            //     return ENOMEM;
            // }
            
            // //get the physical address and the frame number from it
            // paddr_t new_physical_address = KVADDR_TO_PADDR(new_frame);
            // paddr_t new_frame_number = new_physical_address & PAGE_FRAME;
            
            // // Nullify the frame and then move the memory from old frame to new frmae
            // // using the addresses.
            // as_zero_region(new_frame, 1);
            // if (memmove((void *)new_frame, (const void *) 
            // PADDR_TO_KVADDR(oldPT->Pages[i][j][k] & PAGE_FRAME), PAGE_SIZE) == NULL) {
            //     Page_table_free(newPT);
            //     return ENOMEM;
            // }
            
            // // Get the dirtty and valid bit of the old frame and then write 
            // // the value to the correct index.
            // int dirty_bit = oldPT->Pages[i][j][k] & TLBLO_DIRTY;
            // int valid_bit = oldPT->Pages[i][j][k] & TLBLO_VALID;
            // newPT->Pages[i][j][k] = new_frame_number | dirty_bit | valid_bit;

            // Point the new page table entry to the same frame in the memeory initially for the
            // shared pages and copy on write(adv. ass.) will be useful for saving space in Fork.
            oldPT->Pages[i][j][k] &= ~TLBLO_DIRTY;
            newPT->Pages[i][j][k] = oldPT->Pages[i][j][k];
            frame_ref_increase(newPT->Pages[i][j][k] & PAGE_FRAME);

        }
                    
        else {
            newPT->Pages[i][j][k] = 0;
        }

    }

    return SUCCESS;	
}

////////////////////////////////////////////////////////////////////////////////////////
/////////////////////// PAGE_TABLE_ADD AND ASSOCIATED HELPER FUNCS. ////////////////////
////////////////////////////////////////////////////////////////////////////////////////

int Page_table_Add (vaddr_t faultaddress, paddr_t frame_no, Region_t as_reg, HeapRegion_t as_hreg, Mmap_Region_t as_freg ,struct addrspace* as) {

    (void) as_reg;
    // Finding the level 3 index by the 6 bits just after
    // the offset(using approporiate mask 0x3F = (111111))
    faultaddress = faultaddress >> 12;
    uint32_t TLI = faultaddress & 0x3F;
    
    // Finding the level 2 index by the 6 bits just after
    // the Third level 6 bits(using approporiate mask 0x3F = (111111))
    faultaddress = faultaddress >> 6;
    uint32_t SLI = faultaddress & 0x3F;
    
    // Finding the level 1 index by the 8 bits just after
    // the Third level 6 bits(using the appropriate maks 0xFF(11111111))
    faultaddress = faultaddress >> 6;
    uint32_t FLI = faultaddress & 0xFF;

    /* Mechanism to add it on the basis of region
        rwx = DV
        rw- = DV
        r-x = -V
        r-- = -V
        -wx = DV
        -w- = DV
        --x = -V
        --- = -- 
    */
   	uint32_t entry_lo = frame_no;
    
    // make it readable irrespective of whether it is having all other permission or not for the copy_on_write
    // and shared pages.
    //entry_lo |= TLBLO_VALID;

    // if the region is writeable set the dirty bit on.
    if (as_reg != NULL && as_hreg == NULL && as_freg == NULL) {
        
        if (as_reg->writeable) {
            entry_lo |= TLBLO_DIRTY;
        }

        /// if the region is either just readble, writeable or executable 
        // then set the valid bit on.
        if (as_reg->readable != 0 || as_reg->executable != 0 
            || as_reg->writeable != 0) {
            entry_lo |= TLBLO_VALID;
        }

    }

    else if (as_reg == NULL && as_hreg != NULL && as_freg == NULL){

        if (as_hreg->writeable) {
            entry_lo |= TLBLO_DIRTY;
        }

        /// if the region is either just readble, writeable or executable 
        // then set the valid bit on.
        if (as_hreg->readable != 0 || as_hreg->executable != 0 
            || as_hreg->writeable != 0) {
            entry_lo |= TLBLO_VALID;
        }

    }

    else {
        entry_lo = TLBLO_DIRTY | TLBLO_VALID;

    }
	
	// Add the entries in the page table and check whether it is empty
    // and add the entries at the appropriate entry.

    int err_insert = Page_table_Insert(as, FLI, SLI, TLI, entry_lo);

    if (err_insert) {
        return err_insert;
    }

	return SUCCESS;

}

////////////////////////////////////////////////////////////////////////////////////////
/////////////////////// PAGE_TABLE_FREE AND ASSOCIATED HELPER FUNCS. ///////////////////
////////////////////////////////////////////////////////////////////////////////////////

void Page_table_free(Page_table_t pt) {
    
    // check at each level if the entry is not NULL or zero and then
    // free frame for the corresponding entry at the level based on the physical frame address
    // stored in the page table entry.
	for (int i = 0; i < LEVEL1_LIMIT; i++) {
		if (pt->Pages[i] != NULL) {
			for (int j = 0; j < LEVEL2_AND_3_LIMIT; j++) {
				if (pt->Pages[i][j] != NULL) {
					for (int k = 0; k < LEVEL2_AND_3_LIMIT; k++) {
                        if (pt->Pages[i][j][k] != 0) {
                            vaddr_t frame_number = PADDR_TO_KVADDR(pt->Pages[i][j][k] & PAGE_FRAME);
                            if (frame_number != (vaddr_t) NULL) {
                                free_kpages(frame_number);
                            }
                        }
					}
					kfree(pt->Pages[i][j]);
				}
			}
			kfree(pt->Pages[i]);
		}
	}
	
    kfree(pt);

}

////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////// COMMON HELPER FUNCTIONS //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////

// A wrapper which calls bzero and nullifies the address space uptil the
// page limit given where each page is PAGE_SIZE long.
void as_zero_region(vaddr_t vaddr, unsigned npages)
{
	bzero((void *) (vaddr), npages * PAGE_SIZE);
}
