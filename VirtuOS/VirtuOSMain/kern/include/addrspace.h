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

#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

/*
 * Address space structure and operations.
 */


#include <vm.h>
#include "opt-dumbvm.h"

#define SUCCESS 0

struct vnode;


/*
 * Address space - data structure associated with the virtual memory
 * space of a process.
 *
 * You write this.
 */

// Page table struct
struct PageTable {

    paddr_t*** Pages;

}; 

typedef struct PageTable* Page_table_t;

// adresspace region linked list structure
struct addrspace_region {

	vaddr_t base_addr; 
	vaddr_t end_addr;
	int readable; 
	int writeable; 
	int executable;
	bool is_readonly;
	struct addrspace_region* next;
	
};

struct Heap_region {

	vaddr_t  base_heap_addr;
	vaddr_t cur_heap_break;
	int readable; 
	int writeable; 
	int executable;
	struct lock* Heap_lock;
};

struct Mmap_Region {

	vaddr_t         Base_address;
	size_t 			length;
	int 			File_descriptor;
	off_t  			File_offset;
	int 			File_prot;
	int 			Num_pages;
	struct Mmap_Region* next;
}

typedef struct addrspace_region* Region_t;
typedef struct Heap_region* HeapRegion_t;
typedef struct Mmap_Region* Mmap_Region_t;

struct addrspace {

#if OPT_DUMBVM
	vaddr_t as_vbase1;
	paddr_t as_pbase1;
	size_t as_npages1;
	vaddr_t as_vbase2;
	paddr_t as_pbase2;
	size_t as_npages2;
	paddr_t as_stackpbase;

#else
    
	Region_t 		addr_region_base;
	Region_t 		addr_region_end;
	HeapRegion_t    Proc_heap;
	Mmap_Region_t   File_region_base;
	Mmap_Region_t   File_region_end;
	Page_table_t 	PageTable;
	

#endif
};

typedef struct addrspace* addrspace_t;
/*
 * Functions in addrspace.c:
 *
 *    as_create - create a new empty address space. You need to make
 *                sure this gets called in all the right places. You
 *                may find you want to change the argument list. May
 *                return NULL on out-of-memory error.
 *
 *    as_copy   - create a new address space that is an exact copy of
 *                an old one. Probably calls as_create to get a new
 *                empty address space and fill it in, but that's up to
 *                you.
 *
 *    as_activate - make curproc's address space the one currently
 *                "seen" by the processor.
 *
 *    as_deactivate - unload curproc's address space so it isn't
 *                currently "seen" by the processor. This is used to
 *                avoid potentially "seeing" it while it's being
 *                destroyed.
 *
 *    as_destroy - dispose of an address space. You may need to change
 *                the way this works if implementing user-level threads.
 *
 *    as_define_region - set up a region of memory within the address
 *                space.
 *
 *    as_prepare_load - this is called before actually loading from an
 *                executable into the address space.
 *
 *    as_complete_load - this is called when loading from an executable
 *                is complete.
 *
 *    as_define_stack - set up the stack region in the address space.
 *                (Normally called *after* as_complete_load().) Hands
 *                back the initial stack pointer for the new process.
 *
 * Note that when using dumbvm, addrspace.c is not used and these
 * functions are found in dumbvm.c.
 */

struct addrspace *as_create(void);
int               as_copy(struct addrspace *src, struct addrspace **ret);
void              as_activate(void);
void              as_deactivate(void);
void              as_destroy(struct addrspace *);

int               as_define_region(struct addrspace *as,
                                   vaddr_t vaddr, size_t sz,
                                   int readable,
                                   int writeable,
                                   int executable);
int               as_prepare_load(struct addrspace *as);
int               as_complete_load(struct addrspace *as);
int               as_define_stack(struct addrspace *as, vaddr_t *initstackptr);
vaddr_t as_set_process_break(struct addrspace* as, intptr_t amount, int* err_sbrk);
vaddr_t Find_Free_File_Region(struct addrspace* as, vaddr_t base_addr, vaddr_t end_addr);
vaddr_t as_mmap_file(struct addrspace* as, size_t length, int prot, int fd, off_t offset, &err_mmap);

/*
 * Functions in loadelf.c
 *    load_elf - load an ELF user program executable into the current
 *               address space. Returns the entry point (initial PC)
 *               in the space pointed to by ENTRYPOINT.
 */

int load_elf(struct vnode *v, vaddr_t *entrypoint);

////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////// EXTRA FUNCTION DEFINITONS FOR addrspace.h ///////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// ERROR HANDLING FUNCTION DEFINTIONS ////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////

int err_handling_as_define(struct addrspace *as, vaddr_t vaddr, size_t sz);
int regions_overlap(struct addrspace* as, vaddr_t base_addr, vaddr_t end_addr);
int err_handling_as_copy(struct addrspace* old);
int err_handling_comp_load(struct addrspace* as);
int err_handling_as_prep_load(struct addrspace* as);
int err_handling_as_stack(struct addrspace* as);
int err_handling_vm_fault(vaddr_t faultaddress);
int err_handling_vm_fault(vaddr_t faultaddress);

/////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// PAGE TABLE SPECEFIC FUNCTIONs ///////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////
paddr_t Page_table_lookup(struct addrspace* as, vaddr_t faultaddress);
Page_table_t Page_table_Set(int *err_PT_set);
void Page_table_free(Page_table_t pt);
int Page_table_copy(Page_table_t oldPT, Page_table_t newPT);
int Page_table_Add (vaddr_t faultaddress, paddr_t frame_no, Region_t as_reg, HeapRegion_t as_hreg, struct addrspace* as);
int Page_table_Insert(struct addrspace *as, uint32_t FLI, uint32_t SLI, uint32_t TLI, uint32_t entry_lo);
void Page_table_readonly (struct addrspace *as, vaddr_t base_addr);
int init_level_three (struct addrspace *as, uint32_t FLI, uint32_t SLI);
int init_level_two (struct addrspace *as, uint32_t FLI);
int Level_three_copy (Page_table_t oldPT, Page_table_t newPT, int i, int j);
int Level_two_copy (Page_table_t oldPT, Page_table_t newPT, int i);

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////// ADDRESS SPACE SPECEFIC FUNCTIONS ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

int Region_copy (struct addrspace* old, struct addrspace* newas);
void Create_Region (struct addrspace *as, Region_t new_region, 
		vaddr_t vaddr, size_t memsize, int readable, 
		int writeable, int executable);

///////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// VM_FAULT SPECEFIC FUNCTIONS ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

int vm_fault(int faulttype, vaddr_t faultaddress);
int copy_on_write(struct addrspace *as, vaddr_t faultaddress);
int tlb_miss_handler(vaddr_t faultaddress, struct addrspace* as, Region_t Valid_Region, HeapRegion_t Valid_Heap);
void Load_TLB(uint32_t entry_hi, uint32_t entry_lo);
Region_t Lookup_Region(struct addrspace* as, vaddr_t faultaddress);
HeapRegion_t Lookup_Heap(struct addrspace* as, vaddr_t faultaddress);
int Alloc_Frame_Insert_PTE(vaddr_t faultaddress, struct addrspace* as, Region_t as_req, HeapRegion_t as_hreq);

//////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////// COMMON HELPER FUNCTIONS //////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

void as_zero_region(vaddr_t vaddr, unsigned npages);

#endif /* _ADDRSPACE_H_ */
