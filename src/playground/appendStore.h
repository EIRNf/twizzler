#include <twz/obj.h>


// This is a temp fixed length struct to build something with
typedef struct record {
	char name[20];
	int age;
	int sal;
} record;

// Use index to keep track of pointers to keep track of your data
struct appendDatastore_hdr {
	int recordCount;
	void *end; // Pointer to keep track of "append" location in storage
};

void appendDatastore_init(twzobj *dataObj);
void appendDatastore_addRecord (record *row, void** row_pointer);
void appendDatastore_printRecord(twzobj * obj,void *row_pointer);
struct record appendDatastore_retrieveRecord(record *row_pointer);