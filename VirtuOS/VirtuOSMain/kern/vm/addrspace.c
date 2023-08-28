/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <synch.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <elf.h>
#include <../arch/mips/include/vm.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */

/////////////////////////////////////////////////////////////////////////////////
/////////////////// BASIC ASSIGNMENT ADDRESS SPACE FUNCTIONS ////////////////////
/////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////
///////////////////////// AS_CREATE ///////////////////////////
///////////////////////////////////////////////////////////////

// Creates the addresspace for a process.
struct addrspace * as_create(void) {
	
	// kmalloc the memory for the addrespace
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	
	if (as == NULL) {
		return NULL;
	}

	// Init the page table and set it to the pagetable entry in
	// Addresspace
	int err_PT_set = -1;
	as->PageTable = Page_table_Set(&err_PT_set);

	if (err_PT_set!= 0 && as->PageTable == NULL) {
		return NULL;
	}

	// Set the base and end pointers to NULL
	as->addr_region_base = NULL;
	as->addr_region_end = NULL;
	as->Proc_heap = kmalloc(sizeof(struct Heap_region));
	as->Proc_heap->cur_heap_break = (vaddr_t) NULL;
	as->Proc_heap->base_heap_addr = (vaddr_t) NULL;
	as->Proc_heap->readable = PF_R;
	as->Proc_heap->writeable = PF_W;
	as->Proc_heap->executable = 0;
	as->Proc_heap->Heap_lock =  lock_create("Heap Lock created for forking and atomicity");
	as->File_region_base = 	NULL;
	as->File_region_end = NULL;
	return as;
}

///////////////////////////////////////////////////////////////
///////////////////////// AS_COPY /////////////////////////////
///////////////////////////////////////////////////////////////

int as_copy(struct addrspace *old, struct addrspace **ret) {
	
	// Do all the error_checking in the beginning
	int err_copy = err_handling_as_copy(old);

	if (err_copy) {
		return err_copy;
	}

	// Create the new addresspace for the copying the old to it.
	struct addrspace *newas = as_create();
	
	if (newas==NULL) {
		return ENOMEM;
	}

	/* Copy regions */
	int err_region_copy = Region_copy (old, newas);

	if (err_region_copy) {
		return err_region_copy;
	}

	newas->Proc_heap->base_heap_addr = old->Proc_heap->base_heap_addr;
	
	newas->Proc_heap->cur_heap_break = old->Proc_heap->cur_heap_break;
	

	/* Copy the pagetable from old to new */
	int copy_pt = Page_table_copy(old->PageTable, newas->PageTable);
	
	if (copy_pt) {
		as_destroy(newas);
		return copy_pt;
	}

	// Set *ret pointer to the new addresspace.
	*ret = newas;
	return SUCCESS;
}

///////////////////////////////////////////////////////////////
///////////////////////// AS_DESTROY //////////////////////////
///////////////////////////////////////////////////////////////

// Destroys the address space by removing all the regions , 
// page table and the assocaited frames.
void as_destroy(struct addrspace *as) {
	
	/* deep-clean regions */
	Region_t curr = as->addr_region_base;
	Region_t addr_destroy = NULL;
	
	while (curr != NULL) {
		// Grab the region to be freed
		addr_destroy = curr;

		// Go to the next one
		curr = curr->next;

		// Free the address
		kfree(addr_destroy);
	}
	
	// Free page table entries
	Page_table_free(as->PageTable);
	kfree(as->Proc_heap);
	kfree(as);

}

///////////////////////////////////////////////////////////////
///////////////////////// AS_ACTIVATE /////////////////////////
///////////////////////////////////////////////////////////////

// Directly taken from dumbvm.c 
void as_activate(void) {

	struct addrspace *as = proc_getas();
	
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	int spl = splhigh();

	for (int i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

///////////////////////////////////////////////////////////////
///////////////////////// AS_DEACTIVATE ///////////////////////
///////////////////////////////////////////////////////////////

void as_deactivate(void) {
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
	as_activate();
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */

///////////////////////////////////////////////////////////////
///////////////////////// AS_DEFINE_REGION ////////////////////
///////////////////////////////////////////////////////////////

int as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		int readable, int writeable, int executable) {

	/* Align the region. First, the base...  Ued to add all the 
	entries in specefic frames fashion*/
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;
	
	// Do the associated error checking in the beginning.
	int err_as_define = err_handling_as_define(as, vaddr, memsize);

	if (err_as_define) {
		return err_as_define;
	}
	
	// Create the region struct for the region associated with the addresspace
	Region_t new_region = kmalloc(sizeof(struct addrspace_region));

	if (new_region == NULL) {
		return ENOMEM;
	}

	// Add all the values in the region struct created.
	Create_Region (as, new_region, vaddr, memsize, 
	readable, writeable, executable);
	
	return SUCCESS; 
}

///////////////////////////////////////////////////////////////
///////////////////////// AS_PREPARE_LOAD /////////////////////
///////////////////////////////////////////////////////////////

// Set all the readonly regions to READ_WRITE for the initial loading 
// purposes.
int as_prepare_load(struct addrspace *as) {
	
	// Handle all the associated errors in the begininning
	int err_prep_load = err_handling_as_prep_load(as);
	
	if (err_prep_load) {
		return err_prep_load;
	}

	// Loop through all the regions and check if the 
	// region is readonly and if it is then set it to 
	// Read_write for initial loading purposes.
	Region_t as_region = as->addr_region_base;

	while (as_region != NULL) {
		if (as_region->is_readonly == true) {
			as_region->writeable = PF_W;
		}
		as_region = as_region->next;
	}

	return SUCCESS;
}

///////////////////////////////////////////////////////////////
/////////////////////  AS_COMPLETE_LOAD ///////////////////////
///////////////////////////////////////////////////////////////

void Page_table_readonly (struct addrspace *as, vaddr_t base_addr) {

	base_addr = base_addr >> 12;
	uint32_t TLI = base_addr & 0x3F;

	base_addr = base_addr >> 6;
	uint32_t SLI = base_addr & 0x3F;

	base_addr = base_addr >> 6;
	uint32_t FLI = base_addr & 0xFF;

	if (as->PageTable->Pages[FLI][SLI][TLI] != 0) {
		as->PageTable->Pages[FLI][SLI][TLI] &= ~TLBLO_DIRTY;
	}



}

int as_complete_load(struct addrspace *as) {
	
	// Handle all the associated errors in the begininning
	int err_comp_load = err_handling_comp_load(as);
	
	if (err_comp_load) {
		return err_comp_load;
	}

	// Loop through all the regions and check if the 
	// region is readonly and if it has been set to 
	// READ_WRITE then reset it to READONLY after the loading has been 
	// completed.
	Region_t as_region = as->addr_region_base;
	
	while (as_region != NULL) {
		if (as_region->is_readonly == true && as_region->writeable == PF_W) {
			as_region->writeable = 0;
		}
		Page_table_readonly(as, as_region->base_addr);
		as_region = as_region->next;

	}
	
	
	// Flush the TLB entries after this.
	as_activate();

	return SUCCESS;
}

///////////////////////////////////////////////////////////////
/////////////////////// AS_DEFINE_STACK ///////////////////////
///////////////////////////////////////////////////////////////

// Define the stack region, implicitly calls as_define_region with correct 
// base address and the read_write permissions.
int as_define_stack(struct addrspace *as, vaddr_t *stackptr) {
	
	// Handle all the associated errors in the begininning
	int err_stack = err_handling_as_stack(as);
	
	if (err_stack) {
		return err_stack;
	}
	
	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;
	
	vaddr_t starting_address = *stackptr - STACK_LIMIT;
	
	//calling the as_define_region with the 
	// correct starting address for stackpointer and 
	// making it readable and writeable.
	int err_stack_define = as_define_region(as, starting_address, STACK_LIMIT, PF_R, PF_W, 0);

	/* Handles the error for stack */
	if (err_stack_define) {
		return err_stack_define;
	}
	
	return SUCCESS;
}


///////////////////////////////////////////////////////////////////////////////////////////////
/////////////// HELPER FUNCTIONS AND ERROR HANDLING FOR ADDRESS SPACE FUNCTIONS////////////////
///////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////// AS_DEFINE_REGION HELPER FUNCTIONS AND ERROR HANDLING ////////////////
///////////////////////////////////////////////////////////////////////////////////////////////

int err_handling_as_define(struct addrspace *as, vaddr_t vaddr, size_t memsize) {
	
	if (as == NULL) {
		return EFAULT;
	}

	// Checks that the base address lies in the KU_SEG and if the region is 
	// added then it does'nt overlap with the KSEG0.
	if (vaddr + memsize > MIPS_KSEG0 || vaddr > MIPS_KSEG0) {
		return EFAULT;
	}

	// Checks that the region being added has its starting and ending address
	// overlapping with any other Regions and if it has then dont add it and 
	// return EADDRINUSE.
	int err_overlap = regions_overlap(as, vaddr, vaddr+memsize);

	if (err_overlap){
		return err_overlap;
	}

	return SUCCESS;

}

// Loops throug all Regions in an addresspace and Checks if the new region overlapps 
// with the previous regions.
int regions_overlap(struct addrspace* as, vaddr_t base_addr, vaddr_t end_addr) {

	// Get the Base address of the address_space
	Region_t cur_addresses = as->addr_region_base;

	while (cur_addresses != NULL) {

		//Checks if the new region lies inside the current 
		// region being compared with.
		// New_Region -   ##############
		// Cur_region - ##################
		if (base_addr >= cur_addresses->base_addr 
			&& end_addr < cur_addresses->end_addr) {
			return EINVAL;
		}

		// Cheks whether newly added region overlapps with its base 
		// being within the region space of the current region
		//  but end being outside of the region space.
		// New_Region -      ######################
		// Cur_region - ##################
		if (base_addr >= cur_addresses->base_addr 
			&& base_addr < cur_addresses->end_addr
			&& end_addr >= cur_addresses->end_addr) {
			return EINVAL;
		}

		// Checks whether the newly added region overlaps with the
		// current region being checked with its bse being less than the 
		// current base but the end lying with the region space.
		// New_Region -  ######################
		// Cur_region -		  ########################		
		if (base_addr <= cur_addresses->base_addr 
			&& end_addr > cur_addresses->base_addr 
			&& end_addr <= cur_addresses->base_addr) {
			return EINVAL;
		}
		
		//Move to the next region
		cur_addresses = cur_addresses->next;
	}

	return SUCCESS;

}

//Create the region striuct by addin all the bookeeping necessay for each of 
// region linked list structure and then points the begin and end of addrspace to
// the correct values
void Create_Region (struct addrspace *as, Region_t new_region, 
		vaddr_t vaddr, size_t memsize, int readable, 
		int writeable, int executable) {
	
	// Add all the required values in the book-keeping
	new_region->base_addr = vaddr; 
	new_region->end_addr = new_region->base_addr + memsize - 1;
	new_region->readable = readable; 
	new_region->writeable = writeable; 
	new_region->executable = executable;
	new_region->next = NULL;
	
	// if the region is readble set the readonly field to true.
	if (readable == PF_R && writeable == 0) {
		new_region->is_readonly = true;
	}
	
	else {
		new_region->is_readonly = false;
	}
	
	// Set the region_base and end correctly
	if (as->addr_region_base == NULL) {
		as->addr_region_base = new_region;
	}

	if (as->addr_region_end  != NULL) {
		as->addr_region_end->next = new_region;
	}
	
	// Set the end to the newly added region.
	if (new_region->base_addr < USERSTACK - STACK_LIMIT) {
		as->addr_region_end = new_region;
	}
	

}

///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////// AS_COPY HELPER FUNCTIONS AND ERROR HANDLING /////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////

// Handles the null address space by giving EFAULT for it.
int err_handling_as_copy(struct addrspace* old) {

	if (old == NULL) {
		return EFAULT;
	}
	return SUCCESS;
}

// Deep copies all the region entries from the old address space to the new 
// address space.

int Region_copy (struct addrspace* old, struct addrspace* newas) {

	// Loop through all the region entries in the previous addresspace, 
	// and then creates a temp region by mallocing it and copies all the
	//  entries in the to the newas, also sets the region_base and region_end 
	// accordinly while loping/ entering the entries.
	for (Region_t old_regions = old->addr_region_base; 
		old_regions != NULL; old_regions = old_regions -> next) {
		
		// Kmallocs the memory for the region.
		Region_t temp = kmalloc(sizeof(struct addrspace_region));
		
		if (temp == NULL) {
			as_destroy(newas);
			return ENOMEM;
		}

		/* Deep copy old region to temp */
		temp -> base_addr = old_regions -> base_addr;
		temp -> end_addr = old_regions -> end_addr;
		temp -> readable = old_regions -> readable;
		temp -> writeable = old_regions -> writeable;
		temp -> executable = old_regions -> executable;
		temp -> is_readonly = old_regions -> is_readonly;
		temp -> next = NULL;

		// Set the base and end accordinly for the Region Linked list
		if (newas->addr_region_base == NULL) {
			newas->addr_region_base = temp;
		}

		if (newas->addr_region_end  != NULL) {
			newas->addr_region_end->next = temp;
		}

		// Sets the end to the newly added region.
		if (temp->base_addr < USERSTACK - STACK_LIMIT) {
			newas->addr_region_end = temp;
		}

	}

	return SUCCESS;

}

///////////////////////////////////////////////////////////////////////////////////////////////
////////////////////// AS_DEFINE_STACK HELPER FUNCTIONS AND ERROR HANDLING ////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////

// Handles the null address space by giving EFAULT for it.
int err_handling_as_stack(struct addrspace* as) {

	if (as == NULL) {
		return EFAULT;
	}

	return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////
////////////////////// AS_PREPARE_LOAD HELPER FUNCTIONS AND ERROR HANDLING ////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////

// Handles the null address space by giving EFAULT for it.
int err_handling_as_prep_load(struct addrspace* as) {

	if (as == NULL) {
		return EFAULT;
	}

	return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////
////////////////////// AS_COMPLETE_LOAD HELPER FUNCTIONS AND ERROR HANDLING ///////////////////
///////////////////////////////////////////////////////////////////////////////////////////////

// Handles the null address space by giving EFAULT for it.
int err_handling_comp_load(struct addrspace* as) {

	if (as == NULL) {
		return EFAULT;
	}

	return SUCCESS;
}


///////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////// ADVANCED ASSIGNMENT RELATED FUNCTIONS /////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////// AS_SET_PROCESS_BREAK ////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////



vaddr_t as_set_process_break(struct addrspace* as, intptr_t amount, int* err_sbrk) {

	vaddr_t retval;
	
	lock_acquire(as->Proc_heap->Heap_lock);
	if (as->Proc_heap->base_heap_addr == (vaddr_t) NULL && as->Proc_heap->cur_heap_break == (vaddr_t) NULL) {
		as->Proc_heap->base_heap_addr = as->addr_region_end->end_addr + 1;
		as->Proc_heap->cur_heap_break = as->Proc_heap->base_heap_addr;
		retval = as->Proc_heap->base_heap_addr;
	}


	else {

		retval = as->Proc_heap->cur_heap_break;
		
		vaddr_t new_break = as->Proc_heap->cur_heap_break + (vaddr_t) amount;
		
		if (new_break < as->Proc_heap->base_heap_addr) {
			*err_sbrk = EINVAL;
			return (vaddr_t) NULL;
		}

		if (new_break >= USERSTACK - STACK_LIMIT) {
			*err_sbrk = ENOMEM;
			return (vaddr_t) NULL;
		}

		as->Proc_heap->cur_heap_break = new_break;

	}
	lock_release(as->Proc_heap->Heap_lock);

	return retval;
	
}

vaddr_t Find_Free_File_Region(struct addrspace* as, vaddr_t base_addr, vaddr_t end_addr) {

	bool found = false;

	while (found == false) {
		int Overlappin_region = regions_overlap(as, base_addr, end_addr);

		if (Overlappin_region == SUCCESS) {
			return end_addr;
		}

	}

	return NULL;
}

vaddr_t as_mmap_file(struct addrspace* as, size_t length, int prot, int fd, off_t offset, &err_mmap) {

	vaddr_t retval;
	vaddr_t File_Region_base;

	File_Region_base = Find_Free_File_Region(as, as->File_region_end->Base_address, as->File_region_end->Base_address - ROUNDUP(length, PAGE_SIZE));

	if (File_Region_base == NULL) {
		*err_mmap = EFAULT;
		return (vaddr_t) NULL;
	}

	length += File_Region_base & ~(vaddr_t)PAGE_FRAME;
	File_Region_base &= PAGE_FRAME;

	length = (length + PAGE_SIZE - 1) & PAGE_FRAME;

	int num_pages =  length / PAGE_SIZE;

	/* ...and now the length. */
	length = (length + PAGE_SIZE - 1) & PAGE_FRAME;
	
	// Create the region struct for the region associated with the addresspace
	Mmap_Region_t New_mmap = kmalloc(sizeof(struct Mmap_Region));

	if (New_mmap== NULL) {
		*err_mmap = ENOMEM;
		return (vaddr_t) NULL;
	}

	
	New_mmap->Base_address = File_Region_base;
	New_mmap->length = length;
	New_mmap->File_descriptor = fd;
	New_mmap->File_offset = offset;
	New_mmap->File_prot = prot;
	New_mmap->num_pages = num_pages;

	
	if (as->File_region_base == NULL) {
		as->File_region_base = New_mmap;
	}

	if (as->File_region_end  != NULL) {
		as->File_region_end->next = New_mmap;
	}
	
	// Set the end to the newly added region.
	if (New_mmap->base_addr < USERSTACK - STACK_LIMIT) {
		as->File_region_end = New_mmap;
	}

	retval = File_Region_base;
	
	return retval;

}







