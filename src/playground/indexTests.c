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

#define NUM_BUCKETS 10

// Hash Table Struct, try using as Twizzler Object header
typedef struct {
	htItem **items; // Array of pointers to items
	int size;
	int count;
} hashTable;

// Hash Table Item/ Primary Index Key/Value Pair
typedef struct {
	char *tconst;
	struct titleRatings *value;
} htItem;

// Title ratings data
typedef struct {
	float averageRating;
	int numVotes;
} titleRatings;

int main(int argc, char **argv)
{
	return 0;
}

// PLAN OF ATTACK

// IDEALLY use already existing data to build storage, already have a random generator
//  for basic data if this doesnt work.

// At some point structure this program like a sane person instead of just one file

//  Write a simple store
// Make a primary index based on that Hash Table as a primary method of storage and access
// A hash table will actually use dynamic twizzler allocation unlike just a basic array store
// good for actual practice before building the actual indexes

// Structs defined above
// Basic Operations

// hash function
long hash_function(char *str)
{ 
  // Takes the string id from the data(ex. tt000001)
  // this is a very bad hash function
	long i = 0;
	for(int j = 0; str[j]; j++)
		i += str[j];
	return i % NUM_BUCKETS;
}

// create table in persistent memory
twzobj *createTable()
{
	// Create a new HashTable directly in persistent memory

	// Object handler
	twzobj obj;
	/* create an object into handle obj, not copied from an existing object, without a public key,
	 * with default permissions READ and WRITE */

	if(twz_object_new(&obj, NULL, NULL, OBJ_VOLATILE, TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE) < 0) {
		// Handle error
	}

	/* init the object to respond to the default allocation API */
	twz_object_init_alloc(&obj, sizeof(hashTable));

	// Get access to the base of object
	hashTable *htHdr = twz_object_base(&obj);
	/* hdr is now a v-ptr (virtual pointer) that can be dereferenced. It points to the base of the
	 * new object */
	htHdr->size = NUM_BUCKETS;
	htHdr->count = 0;

	// Do I have to preallocate space for the future objects
	// TODO

	return &obj;
}

// create item. Allocates directly from an already existing HT object
htItem *create_item(twzobj obj, char *key, char *value)
{
  hashTable *htHdr = twz_object_base(&obj);
	htItem items = twz_alloc(&obj, 8, &htHdr->items, 0, NULL, NULL);
}
// free item
// free table
// insert
// search
// deletee
// collision handling

// Once ingestion is working and data store functions.
// Get started on those Indexes --->

// Index Operations
//  Scan (fetch all records)
//  Search with Equality Selection
//  Search with Range Selection
//  Insert a record
//  Delete a record

// Secondary Indexes

// Binary index build
// Scan (fetch all records)
// Search with Equality Selection
// Search with Range Selection
// Insert a record
// Delete a record

// B+ index build
// Scan (fetch all records)
// Search with Equality Selection
// Search with Range Selection
// Insert a record
// Delete a record

// Hash index build
