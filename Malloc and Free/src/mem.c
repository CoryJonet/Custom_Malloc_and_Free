#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "mem.h"

/* this structure serves as the header for each block */
typedef struct block_hd{
  /* The blocks are maintained as a linked list */
  /* The blocks are ordered in the increasing order of addresses */
  struct block_hd* next;

  /* size of the block is always a multiple of 4 */
  /* ie, last two bits are always zero - can be used to store other information*/
  /* LSB = 0 => free block */
  /* LSB = 1 => allocated/busy block */

  /* For free block, block size = size_status */
  /* For an allocated block, block size = size_status - 1 */

  /* The size of the block stored here is not the real size of the block */
  /* the size stored here = (size of block) - (size of header) */
  int size_status;

}block_header;

/* Global variable - This will always point to the first block */
/* ie, the block with the lowest address */
block_header* list_head = NULL;


/* Function used to Initialize the memory allocator */
/* Not intended to be called more than once by a program */
/* Argument - sizeOfRegion: Specifies the size of the chunk which needs to be allocated */
/* Returns 0 on success and -1 on failure */
int Mem_Init(int sizeOfRegion)
{
  int pagesize;
  int padsize;
  int fd;
  int alloc_size;
  void* space_ptr;
  static int allocated_once = 0;
  
  if(0 != allocated_once)
  {
    fprintf(stderr,"Error:mem.c: Mem_Init has allocated space during a previous call\n");
    return -1;
  }
  if(sizeOfRegion <= 0)
  {
    fprintf(stderr,"Error:mem.c: Requested block size is not positive\n");
    return -1;
  }

  /* Get the pagesize */
  pagesize = getpagesize();

  /* Calculate padsize as the padding required to round up sizeOfRegio to a multiple of pagesize */
  padsize = sizeOfRegion % pagesize;
  padsize = (pagesize - padsize) % pagesize;

  alloc_size = sizeOfRegion + padsize;

  /* Using mmap to allocate memory */
  fd = open("/dev/zero", O_RDWR);
  if(-1 == fd)
  {
    fprintf(stderr,"Error:mem.c: Cannot open /dev/zero\n");
    return -1;
  }
  space_ptr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (MAP_FAILED == space_ptr)
  {
    fprintf(stderr,"Error:mem.c: mmap cannot allocate space\n");
    allocated_once = 0;
    return -1;
  }
  
  allocated_once = 1;
  
  /* To begin with, there is only one big, free block */
  list_head = (block_header*)space_ptr;
  list_head->next = NULL;
  /* Remember that the 'size' stored in block size excludes the space for the header */
  list_head->size_status = alloc_size - (int)sizeof(block_header);
  
  return 0;
}


/* Function for allocating 'size' bytes. */
/* Returns address of allocated block on success */
/* Returns NULL on failure */
/* Here is what this function should accomplish */
/* - Check for sanity of size - Return NULL when appropriate */
/* - Round up size to a multiple of 4 */
/* - Traverse the list of blocks and allocate the first free block which can accommodate the requested size */
/* -- Also, when allocating a block - split it into two blocks when possible */
/* Tips: Be careful with pointer arithmetic */
void* Mem_Alloc(int size)
{
  /* Your code should go in here */
  if(list_head == NULL)
    {
      return NULL;
    }

  //Round up to a multiple of 4
  if(size % 4 != 0)
    {
      size += (sizeof(block_header) - (size % sizeof(block_header)));
    }

  block_header* current = list_head; //Used to iterate through list (starts a list head)
  block_header* previous = NULL; //Used to point to position behind current
 
  int block_size = size + sizeof(block_header); //Size of the block we are allocating (includes block_header)

  //While current isn't NULL AND the size status of the current block isn't less than the block size we're allocating AND the LSB of current's block header isn't 1 (ie allocated)
  while(current != NULL && current->size_status < block_size && ((current->size_status & 0x1) != 0x1))
    {
      previous = current;
      current = current->next;
    }

  if(current == NULL)
    {
      return NULL;
    }

  block_header* split = (block_header*) ((char*) current + size); //Split after the size of regin block_size
  
  //If current's next isn't NULL, split's next gets current's next
  if(current->next != NULL)
    {
      split->next = current->next;
    }

  //If current's next is NULL, split's next gets NULL
  if(current->next == NULL)
    {
      split->next = NULL;
    }
 
  current->next = split;

  split->size_status = current->size_status - block_size;

  //Set the LSB of current (the block to be used) as 1 (ie its now allocated)
  current->size_status = size |= 0x1; 

  block_header* temp = current + 1; //Return the allocated block + 1 (really + 8 since block header) to return free space to user

  return (void*) temp;

}

/* Function for freeing up a previously allocated block */
/* Argument - ptr: Address of the block to be freed up */
/* Returns 0 on success */
/* Returns -1 on failure */
/* Here is what this function should accomplish */
/* - Return -1 if ptr is NULL */
/* - Return -1 if ptr is not pointing to the first byte of a busy block */
/* - Mark the block as free */
/* - Coalesce if one or both of the immediate neighbours are free */
int Mem_Free(void *ptr)
{
  /* Your code should go in here */
  
  block_header* check = ptr; //Check if ptr is at first byte of a busy block

  //Check if ptr is NULL or is not pointing at a busy block
  if(ptr == NULL || (check->size_status & 0x1) != 0x1)
    {
      return -1;
    }

  block_header* current = list_head; //Used to iterate over list (starts at the list_head)
  block_header* previous = NULL; //Used to point at block behind current
  block_header* next = NULL; //Used to point at block ahead of current

  //Set next 
  if(current->next != NULL)
    {
      next = current->next;
    }

  //While current isn't NULL
  while(current != NULL)
    {
      
      //If the lsb of curent is 1 (just to make sure it's 1 then we can begin process of deallocating)
      if((current->size_status & 0x1) == 0x1)
	{
	  
	  //If current is the same at ptr then we can continue
	  if(current + 1 == (block_header*) ptr)
	    {
	      
	      //If previous and next are NULL (ie current is the only chunk of memory)
	      if(previous == NULL && next == NULL)
		{
		  current->size_status = current->size_status &= ~(0x1);

		  return 0;
		}
	      
	      //If current and next can be coalesced
	      if(previous == NULL && next != NULL)
		{
		  //If next isn't allocated 
		  if((next->size_status & 0x1) != 0x1)
		    {
		      current->size_status = current->size_status + sizeof(block_header) + next->size_status;

		      current->size_status = current->size_status &= ~(0x1);

		      current->next = next->next;
		      
		      return 0;
		    }
		  
		  //If next is allocated
		  if((next->size_status & 0x1) == 0x1)
		    {
		      current->size_status = current->size_status &= ~(0x1);
		      
		      return 0;
		    }
		  
		}
	      
	      //If previous and current can be coalesced
	      if(previous != NULL && next == NULL)
		{
		  //If previous isn't allocated 
		  if((previous->size_status & 0x1) != 0x1)
		    {
		      previous->size_status = previous->size_status + sizeof(block_header) + current->size_status;

		      previous->size_status = previous->size_status &= ~(0x1);

		      previous->next = NULL;

		      return 0;
		    }

		  //If previous is allocated
		  if((previous->size_status & 0x1) == 0x1)
		    {
		      current->size_status = current->size_status &= ~(0x1);

		      return 0;
		    }

		}

	      //If previous, current, and next can be coalesced
	      if(previous != NULL && next != NULL)
		{

		  //If only current is free
		  if(((previous->size_status & 0x1) == 0x1) && ((next->size_status & 0x1) == 0x1))
		    {
		      current->size_status = current->size_status &= ~(0x1);

		      return 0;
		    }

		  //If prev isn't allocated and next isn't allocated
		  if(((previous->size_status & 0x1) != 0x1) && ((next->size_status & 0x1) != 0x1))
		    {
		      previous->size_status = previous->size_status + sizeof(block_header) + current->size_status + sizeof(block_header) + next->size_status;

		      previous->size_status = previous->size_status &= ~(0x1);

		      //If next's next value isn't NULL (ie assign previous's next to next's next)
		      if(next->next != NULL)
			{
			  previous->next = next->next;
			}

		      //If next's value is NULL
		      if(next->next == NULL)
			{
			  previous->next = NULL;
			}
		      
		      return 0;
		    }

		  //If prev is allocated and next isn't allocated
		  if(((previous->size_status & 0x1) == 0x1) && ((next->size_status & 0x1) != 0x1))
		    {
		      current->size_status = current->size_status + sizeof(block_header) + next->size_status;

		      current->size_status = current->size_status &= ~(0x1);
		      
		      //If next's next isn't NULL (ie current's next gets next's next)
		      if(next->next != NULL)
			{
			  current->next = next->next;
			}

		      //If next's next is NULL
		      if(next->next == NULL)
			{
			  current->next = NULL;
			}

		      return 0;
		    }

		  //If prev isn't allocated and next is allocated
		  if(((previous->size_status & 0x1) != 0x1) && ((next->size_status & 0x1) == 0x1))
		    {
		      previous->size_status = previous->size_status + sizeof(block_header) + current->size_status;

		      previous->size_status = previous->size_status &= ~(0x1);

		      previous->next = current->next;

		      return 0;
		    }

		}
	    }
	}

      previous = current;
      current = current->next;
      next = current->next;
	
    }

  return -1; //If all else, something failed and return -1
}

/* Function to be used for debug */
/* Prints out a list of all the blocks along with the following information for each block */
/* No.      : Serial number of the block */
/* Status   : free/busy */
/* Begin    : Address of the first useful byte in the block */
/* End      : Address of the last byte in the block */
/* Size     : Size of the block (excluding the header) */
/* t_Size   : Size of the block (including the header) */
/* t_Begin  : Address of the first byte in the block (this is where the header starts) */
void Mem_Dump()
{
  int counter;
  block_header* current = NULL;
  char* t_Begin = NULL;
  char* Begin = NULL;
  int Size;
  int t_Size;
  char* End = NULL;
  int free_size;
  int busy_size;
  int total_size;
  char status[5];

  free_size = 0;
  busy_size = 0;
  total_size = 0;
  current = list_head;
  counter = 1;
  fprintf(stdout,"************************************Block list***********************************\n");
  fprintf(stdout,"No.\tStatus\tBegin\t\tEnd\t\tSize\tt_Size\tt_Begin\n");
  fprintf(stdout,"---------------------------------------------------------------------------------\n");
  while(NULL != current)
  {
    t_Begin = (char*)current;
    Begin = t_Begin + (int)sizeof(block_header);
    Size = current->size_status;
    strcpy(status,"Free");
    if(Size & 1) /*LSB = 1 => busy block*/
    {
      strcpy(status,"Busy");
      Size = Size - 1; /*Minus one for ignoring status in busy block*/
      t_Size = Size + (int)sizeof(block_header);
      busy_size = busy_size + t_Size;
    }
    else
    {
      t_Size = Size + (int)sizeof(block_header);
      free_size = free_size + t_Size;
    }
    End = Begin + Size;
    fprintf(stdout,"%d\t%s\t0x%08lx\t0x%08lx\t%d\t%d\t0x%08lx\n",counter,status,(unsigned long int)Begin,(unsigned long int)End,Size,t_Size,(unsigned long int)t_Begin);
    total_size = total_size + t_Size;
    current = current->next;
    counter = counter + 1;
  }
  fprintf(stdout,"---------------------------------------------------------------------------------\n");
  fprintf(stdout,"*********************************************************************************\n");

  fprintf(stdout,"Total busy size = %d\n",busy_size);
  fprintf(stdout,"Total free size = %d\n",free_size);
  fprintf(stdout,"Total size = %d\n",busy_size+free_size);
  fprintf(stdout,"*********************************************************************************\n");
  fflush(stdout);
  return;
}
