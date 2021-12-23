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

#define TOTAL_EMPLOYEES 100
#define NUM_BUCKETS 10


// Basic Store
typedef struct {
    char name[20];
    int age;
    int sal;
} employeeRecord; 

typedef struct {
    employeeRecord staff[TOTAL_EMPLOYEES];
    int employeeCount;
} trivialDb;


struct trivialdb_hdr{
    int x;
    trivialDb db;
};

//Hash table key Value item
typedef struct  {
    char* tconst;
    struct titleRatings* value;
} ht_item ;

//Title ratings data
typedef struct {//This is a unique ID should maybe use for key
    float averageRating;
    int numVotes;
} titleRatings;


// Helper function declarations
void randString(char* word, int size);
trivialDb* populateTestDbWithRand(trivialDb* db, int numRecords);
void persistDB(trivialDb* db, twzobj obj);
// trivialDb* readAndLoadPersistedDb();
  
//Helper method to get a random string  
void randString(char* word, int size){
      for( int i = 0; i < size; i++){
          word[i] = '0' + rand()%72; //rand ascii string
      }   
  }


trivialDb* populateTestDbWithRand(trivialDb* db, int numRecords){
      employeeRecord temp;
      char name[20];
  
      for(int i =0; i < numRecords; i++){
          temp.age =  rand();
          temp.sal = rand();
        randString(name, 20);
          strcpy(temp.name , name);
          db->staff[i] = temp;
      }   
  return db; 
}

//Persist the DB object
void persistDB(trivialDb* db, twzobj obj){
    // Object handler
    //twzobj obj;
    /* create an object into handle obj, not copied from an existing object, without a public key,
	 * with default permissions READ and WRITE */
  	if(twz_object_new(&obj, NULL, NULL, OBJ_VOLATILE, TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE) < 0){
          // Handle error
      }

	/* init the object to respond to the default allocation API */
	// twz_object_init_alloc(&obj, sizeof(struct trivialdb_hdr));

    // Get access to the base of object
    struct trivialdb_hdr *hdr = twz_object_base(&obj);
    /* hdr is now a v-ptr (virtual pointer) that can be dereferenced. It points to the base of the
	 * new object */   

    hdr->x = rand(); 
    hdr->db = *db; 

    //return &obj;
}

int main(int argc, char **argv){

    trivialDb *db;
    db = malloc(sizeof(trivialDb));
    populateTestDbWithRand(db, TOTAL_EMPLOYEES);

	twzobj obj;
    persistDB(db, obj);

    struct trivialdb_hdr *hdrPreName = twz_object_base(&obj);
    printf("value: %d\n", hdrPreName->x);
    printf("value: %d \n", hdrPreName->db.employeeCount);
    printf("value: %s \n", hdrPreName->db.staff[0].name);
    
    //name object
    twz_name_dfl_assign(twz_object_guid(&obj), "trivialDBtest");

    //release object
    twz_object_release(&obj);

    //open by name
    //new twizzler object pointer
    //twzobj obj; 

    //Reuse twizzler object hanlder
    if(twz_object_init_name(&obj, "trivialDBtest", FE_READ | FE_WRITE) < 0){
	    // Handle error
    }

    struct trivialdb_hdr *hdrPostName = twz_object_base(&obj);
    printf("value: %d\n", hdrPostName->x);
    printf("value: %d \n", hdrPostName->db.employeeCount);
    printf("value: %s \n", hdrPostName->db.staff[0].name);

    
    
    /* now we can allocate from the object. Here's some size-8 data. Note this API requires
	 * ownership of the allocated data to be transferred as part of the allocation. */
	//int r = twz_alloc(&obj, 8, &hdrPostName->db.staff, 0, NULL, NULL);
	//printf("Allocated: %d %p\n", r, hdr->some_data);

    //fgets()

    return 0;
}

//  Write a simple store, then


// Make Secondary indexes based on that Hash Table

//Util Methods

//Secondary Indexes 

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
// Scan (fetch all records)
// Search with Equality Selection
// Search with Range Selection
// Insert a record
// Delete a record