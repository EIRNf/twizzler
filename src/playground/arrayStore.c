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

#include "arrayStore.h"

// #define ARRAY_SIZE 100

// static twzobj *arrayDatastore; 
static twzobj *arrayDatastore;
static struct arrayDatastore_hdr *hdr;

void arrayDatastore_init(twzobj *dataObj){
    //Create Object
    arrayDatastore = dataObj;

    int r;
	if((r = twz_object_new(arrayDatastore, NULL, NULL, OBJ_PERSISTENT, TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE))){
        		abort();
    }

    hdr = twz_object_base(arrayDatastore); //virtual pointer dpointer

    //Add one to record count
    hdr->records = malloc(RECORDMAX * sizeof(record));
    hdr->size = 0;;

}

//Feed a record of data and add to array, stop before getting to RECORDMAX
void arrayDatastore_addRecord (record *row, int id) {
    //Add one to record count
    hdr->records[id] = *row;
    hdr->size++;
    // Data has been added to storage
}

//Feed persistant pointer and print its values
void arrayDatastore_printRecord(int id){    
    record row = hdr->records[id];

    printf("Name: %s\n", row.name);
    printf("Age: %d\n", row.age);
    printf("Salary: %d \n", row.sal);

}

//Feed persistant pointer and print its values
record arrayDatastore_retrieveRecord(int id){    
    record row = hdr->records[id];

    printf("Name: %s\n", row.name);
    printf("Age: %d\n", row.age);
    printf("Salary: %d \n", row.sal);

    //Return record 
    return row; 
}

/*
int main(int argc, char **argv){
    twzobj datao;
    arrayDatastore_init(&datao);

    printf("size: %d\n", hdr->size);

    int id = 0;
    record rowOne = {"john", id, 1000};
    arrayDatastore_addRecord(&rowOne, id);
    arrayDatastore_printRecord(id);

    id =1;
    record rowTwo = {"sally", id, 2000};

    arrayDatastore_addRecord(&rowTwo, id);
    arrayDatastore_printRecord(id);
    
//     struct record rowThree = {"bob", 30, 1500};
//     void *rowThreePointer;

//   arrayDatastore_addRecord(&rowThree, &rowThreePointer);
//     arrayDatastore_printRecord(rowThreePointer);
//     printf("rowTwoPointer: %p\n", rowThreePointer);
//     printf("hdrRecordCount: %d\n", hdr->recordCount);
//     printf("hdrEndPointer: %p\n", hdr->end);
    


//     arrayDatastore_printRecord(rowOnePointer);
//     arrayDatastore_printRecord(rowTwoPointer);
//     arrayDatastore_printRecord(rowThreePointer);
//     printf("hdrRecordCount: %d\n", hdr->recordCount);
//     printf("hdrEndPointer: %p\n", hdr->end);
}
*/

