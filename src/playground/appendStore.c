/* POSIX includes*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


/* access to the twizzler object API */
#include <twz/alloc.h>
#include <twz/meta.h>
#include <twz/name.h>
#include <twz/obj.h>
#include <twz/ptr.h>

/* Header files for Memory Access*/

#include "appendStore.h"

// #define ARRAY_SIZE 100

// static twzobj *arrayDatastore; 
static twzobj *appendDatastore;

// // Use array indeces as id, with pointers to internal location?
// struct arrayDatastore_hdr {
//     void *array[ARRAY_SIZE]; //Array of pointers that keep track of internal objects
// };

// // This is a temp fixed length struct to build something with
// struct record {
//     char name[20];
//     int age;
//     int sal;
// };

// // Use index to keep track of pointers to keep track of your data
// struct appendDatastore_hdr {
//     int recordCount;
//     void *end; //Pointer to keep track of "append" location in storage
// };

void appendDatastore_init(twzobj *dataObj){
    //Create Object
    appendDatastore = dataObj;

    int r;
	if((r = twz_object_new(appendDatastore, NULL, NULL, OBJ_PERSISTENT, TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE))){
        		abort();
    }

    //TODO: Understand this!
    struct appendDatastore_hdr *hdr = twz_object_base(appendDatastore); //virtual pointer dpointer
            
            // printf(" %p \n",hdr);
            // printf(" %lu, %lu \n",sizeof(hdr->end), sizeof(hdr->recordCount));

            // printf(" %p \n",hdr+1);
            // printf(" %p \n",twz_ptr_local(hdr + 1));

    // value gets written to a memory location, it has not necessarily made it to memory, still in cache,
    // for it to be persisted we flush cache line and have it go to persitent storage
    // to explicitly flush it, call _clwb_len to persist and then call pfence to serialize your cache line
    // in persist.h. Otherwise memory gets persistent when the cache line
    // gets evicted,
    // //Set record count
    hdr->recordCount = 0;
    // Set end pointer to data after header???
    hdr->end = twz_ptr_local(hdr + 1); //Pointer math! T *t = 0x1000; t + 1 is the same as 0x1000 + sizeof(T) 

    //Allocate header to appendDatastore
	//int k = twz_alloc(appendDatastore, 8, &hdr->end, 0, NULL, NULL);
    
    // take p-pointer to d
    //struct appendDatastore_hdr *v = twz_object_lea(appendDatastore, hdr->end);
    //v->recordCount = 0;
    //v->end = twz_ptr_local(hdr + 1); 
    
    //We should now have a pointer in the header to where we can begin to add data
}

//Feed a record of data and a temp pointer** to store persistant poitner to data
void appendDatastore_addRecord (record *row, void** row_pointer) {
    
    struct appendDatastore_hdr *hdr = twz_object_base(appendDatastore); 

    //Add one to record count
    hdr->recordCount = hdr->recordCount + 1;

    //Find pointer to end of data
    void *endPointer = hdr->end;
    //Remember pointer being used for this data(This works if other object)
    // *row_pointer = twz_ptr_swizzle(appendDatastore, endPointer, FE_READ | FE_WRITE);

    // New end pointer points at location of prev end pointer plus length of record
    hdr->end = (void *) ((uintptr_t) hdr->end +  sizeof(record));

    //load a d-pointer from the persistent pointer
    record *newRow = twz_object_lea(appendDatastore, endPointer); 

    //Remember pointer being used
    twz_ptr_store_guid(
        appendDatastore,
        row_pointer,
        NULL,
        newRow,
        FE_READ | FE_WRITE
    );
    strcpy(newRow->name, row->name);
    newRow->age = row->age;
    newRow->sal = row->sal;
    // Data has been added to storage
}

//Feed persistant pointer and print its values
void appendDatastore_printRecord(twzobj * obj, void *row_pointer){    
    struct record *row = twz_object_lea(obj, row_pointer); 

    printf("Name: %s\n", row->name);
    printf("Age: %d\n", row->age);
    printf("Salary: %d \n", row->sal);

}

//Feed persistant pointer and print its values
struct record appendDatastore_retrieveRecord(record *row_pointer){    
    record *row = twz_object_lea(appendDatastore, row_pointer); 

    printf("Name: %s\n", row->name);
    printf("Age: %d\n", row->age);
    printf("Salary: %d \n", row->sal);

    //Return record 
    return *row; 
}

/*
int main(int argc, char **argv){
    twzobj datao;
    appendDatastore_init(&datao);

    // struct appendDatastore_hdr *hdr = twz_object_base(appendDatastore); 

//     printf("hdrRecordCount: %d\n", hdr->recordCount);
//     printf("hdrEndPointer: %p\n", hdr->end);

//     struct record rowOne = {"john", 20, 1000};
//     void *rowOnePointer;

//     appendDatastore_addRecord(&rowOne, &rowOnePointer);
//     appendDatastore_printRecord(rowOnePointer);

//     printf("rowOnePointer: %p\n", rowOnePointer);
//     printf("hdrRecordCount: %d\n", hdr->recordCount);
//     printf("hdrEndPointer: %p\n", hdr->end);

//     struct record rowTwo = {"sally", 25, 2000};
//     void *rowTwoPointer;

//     appendDatastore_addRecord(&rowTwo, &rowTwoPointer);
//     appendDatastore_printRecord(rowTwoPointer);
//     printf("rowTwoPointer: %p\n", rowTwoPointer);
//     printf("hdrRecordCount: %d\n", hdr->recordCount);
//     printf("hdrEndPointer: %p\n", hdr->end);

    
//     struct record rowThree = {"bob", 30, 1500};
//     void *rowThreePointer;

//   appendDatastore_addRecord(&rowThree, &rowThreePointer);
//     appendDatastore_printRecord(rowThreePointer);
//     printf("rowTwoPointer: %p\n", rowThreePointer);
//     printf("hdrRecordCount: %d\n", hdr->recordCount);
//     printf("hdrEndPointer: %p\n", hdr->end);
    


//     appendDatastore_printRecord(rowOnePointer);
//     appendDatastore_printRecord(rowTwoPointer);
//     appendDatastore_printRecord(rowThreePointer);
//     printf("hdrRecordCount: %d\n", hdr->recordCount);
//     printf("hdrEndPointer: %p\n", hdr->end);
}

*/