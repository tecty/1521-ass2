// PageTable.c ... implementation of Page Table operations
// COMP1521 17s2 Assignment 2
// Written by John Shepherd, September 2017

#include <stdlib.h>
#include <stdio.h>
#include "Memory.h"
#include "Stats.h"
#include "PageTable.h"

// Symbolic constants

#define NOT_USED 0
#define IN_MEMORY 1
#define ON_DISK 2

// PTE = Page Table Entry

typedef struct {
   char status;      // NOT_USED, IN_MEMORY, ON_DISK
   char modified;    // boolean: changed since loaded
   int  frame;       // memory frame holding this page
   int  accessTime;  // clock tick for last access
   int  loadTime;    // clock tick for last time loaded
   int  nPeeks;      // total number times this page read
   int  nPokes;      // total number times this page modified
   // field for clock swipe
   int referenced;

} PTE;

// The virtual address space of the process is managed
//  by an array of Page Table Entries (PTEs)
// The Page Table is not directly accessible outside
//  this file (hence the static declaration)

static PTE *PageTable;      // array of page table entries
static int  nPages;         // # entries in page table
static int  replacePolicy;  // how to do page replacement
static int  fifoList;       // index of first PTE in FIFO list
static int  fifoLast;       // index of last PTE in FIFO list

// The double link list is for queing of fifo or lru
typedef struct dll_node_t {
    // record the page number
    int pno;
    // proint to the previou node
    struct dll_node_t *prev;
    // proint to the next node
    struct dll_node_t *next;
} *dll_n;

typedef struct dll_tree_t {
    // it's a circle dll, so the hand -> prev is the tail
    dll_n hand;
} *dll;


// for the queuing of access
dll queue;

dll_n newNode(int pno){
    // create of a new node
    dll_n this = malloc(sizeof(struct dll_node_t));
    if (this == NULL) {
        /* node create error */
        fprintf(stderr, "Couldn't malloc for queue node\n" );
        exit(EXIT_FAILURE);
    }
    // init its val
    this->prev = this->next = NULL;
    this->pno = pno;
    return this;
}

// find the node with pageNo in the queue
dll_n findNode(int pno){
    if (queue->hand->pno == pno) {
        /* queue->hand is the node require */
        return queue->hand;
    }
    for (dll_n this_node = queue->hand->next; this_node != queue->hand;
         this_node = this_node->next) {
         /* look throught the whole array */
         if (this_node->pno == pno) {
             /* this_node is required */
             return this_node;
         }
    }
    // Couldn't find the node
    return NULL;
}

void printQueue(){
#ifdef MYDEBUG
    dll_n this_node = queue->hand;
    printf("%d", this_node->pno );
    for (this_node = this_node->next; this_node != queue->hand; this_node = this_node-> next) {
        /* print all the content in the queue */
        printf(" -> %d",this_node->pno );
    }
    printf("\n");
#endif

}

//  functions for manage the queue
void pushQueue(int pno){
    // push the record in queue by provide pageNo
    if (queue->hand == NULL) {
        /* this is the first node */
        dll_n this_node = newNode(pno);
        // arrange the link
        this_node->prev = this_node->next = this_node;
        // put this node into queue
        queue->hand = this_node;
        // end this function
        return;
    }
    // normal pushing
    dll_n this_node = findNode(pno);
    if (this_node == NULL) {
        /* this page has note been accessed */
        this_node = newNode(pno);
    }
    else {
        // push back this node to the tail to of the queue
        // stitching the gap, caused by the leaving of this_node
        if (this_node == queue->hand) {
            /* prevent the hand point to only this_node */
            queue->hand = queue->hand->next;
        }


        this_node->prev->next = this_node->next;
        this_node->next->prev = this_node->prev;

    }
    // push this_node at tail
    this_node->prev = queue->hand->prev;
    queue->hand->prev->next = this_node;
    queue->hand->prev = this_node;
    this_node->next = queue->hand;
    printQueue();
}






// Forward refs for private functions

static int findVictim(int);

// initPageTable: create/initialise Page Table data structures

void initPageTable(int policy, int np)
{
   PageTable = malloc(np * sizeof(PTE));
   if (PageTable == NULL) {
      fprintf(stderr, "Can't initialise Memory\n");
      exit(EXIT_FAILURE);
   }
   replacePolicy = policy;
   nPages = np;
   fifoList = 0;
   fifoLast = nPages-1;
   for (int i = 0; i < nPages; i++) {
      PTE *p = &PageTable[i];
      p->status = NOT_USED;
      p->modified = 0;
      p->frame = NONE;
      p->accessTime = NONE;
      p->loadTime = NONE;
      p->nPeeks = p->nPokes = 0;
      // flag for clock swipe
      p->referenced = 0;
   }
   // initial the queue
   queue = malloc(sizeof(struct dll_tree_t));
   if (queue == NULL) {
       /* couldn't allocate memory for queue */
       fprintf(stderr, "Couldn't malloc memory for queue\n" );
       exit(EXIT_FAILURE);
   }
   queue->hand = NULL;

}

// requestPage: request access to page pno in mode
// returns memory frame holding this page
// page may have to be loaded
// PTE(status,modified,frame,accessTime,nextPage,nPeeks,nWrites)

int requestPage(int pno, char mode, int time)
{
   if (pno < 0 || pno >= nPages) {
      fprintf(stderr,"Invalid page reference\n");
      exit(EXIT_FAILURE);
   }
   PTE *p = &PageTable[pno];
   int fno; // frame number
   switch (p->status) {
   case NOT_USED:
   case ON_DISK:
      //  add stats collection
      countPageFault();
      fno = findFreeFrame();
      if (fno == NONE) {
         int vno = findVictim(time);
#ifdef DBUG
         printf("Evict page %d\n",vno);
#endif

         // if victim page modified, save its frame
         if (PageTable[vno].modified == 1) {
             /* save this page */
             saveFrame(PageTable[vno].frame);
         }
         // collect frame# (fno) for victim page
         fno = PageTable[vno].frame;
         // update PTE for victim page
         // - new status
         PageTable[vno].status = ON_DISK;
         // - no longer modified
         PageTable[vno].modified = 0;
         // - no longer referenced
         PageTable[vno].referenced = 0;
         // - no frame mapping
         PageTable[vno].frame = NONE;
         // - not accessed, not loaded
         PageTable[vno].accessTime = PageTable[vno].loadTime = NONE;
      }
      printf("Page %d given frame %d\n",pno,fno);

      // load page pno into frame fno
      loadFrame(fno, pno, time);
      // update PTE for page
      // - new status
      p->status = IN_MEMORY;
      // - not yet modified
      p->modified = 0;
      // - associated with frame fno
      p->frame = fno;
      // - just loaded
      p->loadTime = time;

      // record in queue for FIFO
      if (replacePolicy == REPL_FIFO) {
          pushQueue(pno);
      }


      break;
   case IN_MEMORY:
      // add stats collection
      countPageHit();
      break;
   default:
      fprintf(stderr,"Invalid page status\n");
      exit(EXIT_FAILURE);
   }
   if (mode == 'r')
      p->nPeeks++;
   else if (mode == 'w') {
      p->nPokes++;
      p->modified = 1;
   }
   p->accessTime = time;
   // record the info require by LRU or Clock
   if (replacePolicy == REPL_LRU || replacePolicy == REPL_CLOCK) {
       pushQueue(pno);
       p->referenced = 1;
   }

   return p->frame;
}

// findVictim: find a page to be replaced
// uses the configured replacement policy

static int findVictim(int time)
{
    int victim = NONE;
    // record the node would be delete
    dll_n this_node = NULL;

    switch (replacePolicy) {
        case REPL_LRU:
        // implement LRU strategy
        case REPL_FIFO:
            //  implement FIFO strategy

            //  return the tail of the queue;
            victim = queue->hand->pno;

            // free the memory used by node record to prevent memory leak
            this_node = queue->hand;
            //stitching the gap, caused by the leaving of this_node
            if (this_node->prev == this_node) {
            /* the queue only have this_node */
                queue->hand = NULL;
            }
            else{
                // rearrange the link
                this_node->prev->next = this_node->next;
                this_node->next->prev = this_node->prev;
                // assign the new tail for the queue
                queue->hand = this_node->next;
            }
            // free the memory
            free(this_node);


            break;
        case REPL_CLOCK:
            // iterate the hand to find the referenced is 0
            while (PageTable[queue->hand->pno].referenced !=0) {
                /* iterate the queue */
                // reset the referenced flag
                PageTable[queue->hand->pno].referenced = 0;
                // go to next node that recorded
                queue->hand = queue->hand->next;
            }


            //  return the tail of the queue;
            victim = queue->hand->pno;

            // free the memory used by node record to prevent memory leak
            this_node = queue->hand;
            //stitching the gap, caused by the leaving of this_node
            if (this_node->prev == this_node) {
                /* the queue only have this_node */
                queue->hand = NULL;
            }
            else{
                // rearrange the link
                this_node->prev->next = this_node->next;
                this_node->next->prev = this_node->prev;
                // assign the new tail for the queue
                queue->hand = this_node->next;
            }
            // free the memory
            free(this_node);

            break;

    }
    return victim;
}

// showPageTableStatus: dump page table
// PTE(status,modified,frame,accessTime,nextPage,nPeeks,nWrites)

void showPageTableStatus(void)
{
   char *s;
   printf("%4s %6s %4s %6s %7s %7s %7s %7s\n",
          "Page","Status","Mod?","Frame","Acc(t)","Load(t)","#Peeks","#Pokes");
   for (int i = 0; i < nPages; i++) {
      PTE *p = &PageTable[i];
      printf("[%02d]", i);
      switch (p->status) {
      case NOT_USED:  s = "-"; break;
      case IN_MEMORY: s = "mem"; break;
      case ON_DISK:   s = "disk"; break;
      }
      printf(" %6s", s);
      printf(" %4s", p->modified ? "yes" : "no");
      if (p->frame == NONE)
         printf(" %6s", "-");
      else
         printf(" %6d", p->frame);
      if (p->accessTime == NONE)
         printf(" %7s", "-");
      else
         printf(" %7d", p->accessTime);
      if (p->loadTime == NONE)
         printf(" %7s", "-");
      else
         printf(" %7d", p->loadTime);
      printf(" %7d", p->nPeeks);
      printf(" %7d", p->nPokes);
      printf("\n");
   }
}
