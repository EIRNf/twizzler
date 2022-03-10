#include <twz/obj.h>
#include "appendStore.h"

#define RECORDMAX 10

// Use index to keep track of pointers to keep track of your data
struct arrayDatastore_hdr {
	record * records; //Array of all the record, its gonna be big
	int size;
};

void arrayDatastore_init(twzobj *dataObj);
void arrayDatastore_addRecord (record *row, int id);
void arrayDatastore_printRecord(int id);
record arrayDatastore_retrieveRecord(int id);