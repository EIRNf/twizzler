/* POSIX includes*/
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>


/* access to the twizzler object API */
#include <twz/alloc.h>
#include <twz/meta.h>
#include <twz/name.h>
#include <twz/obj.h>
#include <twz/ptr.h>

#include "appendStore.h"

/* Macros */
//332
#define DEFAULT_ORDER 3
#define MIN_ORDER 3
#define MAX_ORDER 20
#define PAGE_SIZE 4000


// Size of a node
typedef struct node {
	void *pointers[DEFAULT_ORDER+1]; // 8 * (Default Order +1) Extra pointer for rightmost pointer
    int keys[DEFAULT_ORDER]; // 4 *(Default Order )
 	void * parent; // 8 bytes
	bool is_leaf; // 4 byte?
	int num_keys; // 4 bytes
	struct node * next; // Used for queue.
}node; 

typedef struct tree_hdr {
 	void *root;
	int numberOfNodes; 
} tree_hdr;

// Static global objects
static twzobj *bTreeObject;
static tree_hdr *hdr;
node * queue = NULL;


// Output and utility.
void tree_init(twzobj *dataObj);
int height(node * const root);
int path_to_root(node * const root, node * child);
node * make_node();
node * make_leaf();
node * find_leaf(int key);
void *  find_record(int key);
record * make_record(char name[20],int age, int sal);
void print_node(node * test);
void print_tree();
void enqueue(node * new_node);
node * dequeue(void);
int path_to_root(node * const root, node * child) ;
int cut(int length);

// int cut(int length);
tree_hdr * bulk_generation();

// Insertion.
void insert (int key, void * recordPointer);
void insert_in_leaf(node * leafNode, int key, void * recordPointer);
void insert_in_parent(node * oldNode, int newKey, node * newNode);
void insert_in_node(node * oldParent, int oldNodePos, int key, node * newNode);



int main(int argc, char **argv)
{

    twzobj datao;
    appendDatastore_init(&datao);

	twzobj dataObj; 
	tree_init(&dataObj);

	//make hdr if it doesnt exist
	hdr = twz_object_base(bTreeObject);

	bulk_generation();

	appendDatastore_printRecord(&datao,find_record(2));
	appendDatastore_printRecord(&datao,find_record(7));
	appendDatastore_printRecord(&datao,find_record(12));
	return 0;
}


void tree_init(twzobj *dataObj) {

	//Create Object for index
	bTreeObject = dataObj;

    int r;
	if((r = twz_object_new(bTreeObject, NULL, NULL, OBJ_PERSISTENT, TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE))){
        		abort();
    }

}

tree_hdr * bulk_generation(){

	//Insert incrementing records
	int numElements = 15;
	for(int i = 0; i < numElements; i++){
		//int k = i | (int) 0 << 16;
		insert(i, (void*) make_record("tempName",i,i*i));
			// printf("_____________________________________________________\n");
			// print_tree();

	}

	printf("size of node: %lu\n",sizeof(node));
	printf("_____________________________________________________\n");
	print_tree();


	/*
	Experiment 1:
		Build and insert cost
	*/
	/*
	Experiment 2:
		Find accross the whole tree
	*/
	/*
	Experiment 3:
		Find accross the whole tree at a bigger scale
	*/
	/*
	Experiment 4:
		Find focused on certain ranges
	*/
	
	/*
	Experiment :
		Random sorted data
	*/
	return hdr;
}

//Print a tree
void print_tree(){
	node * currentNode = NULL;
	int i = 0;
	int rank = 0;
	int new_rank = 0;

	node * root = hdr->root;
	node * oldParent = NULL;
	queue = NULL;

	enqueue(root);
	while (queue != NULL){
		currentNode = (node*)dequeue();
		oldParent = (node*)currentNode->parent;
		
		if (currentNode->parent != NULL && currentNode == oldParent->pointers[0]) {
			new_rank = path_to_root(root, currentNode);
			if (new_rank != rank) {
				rank = new_rank;
				printf("\n");
			}
		}
		printf("(%p)", currentNode);
		for (i = 0; i < currentNode->num_keys; i++) {
			printf("%p ", currentNode->pointers[i]);
			printf("%d ", currentNode->keys[i]);
		}
		if (!currentNode->is_leaf)
			for (i = 0; i <= currentNode->num_keys; i++)
				enqueue(currentNode->pointers[i]);
		if (currentNode->is_leaf)
				printf("%p ", currentNode->pointers[DEFAULT_ORDER ]);
		else
				printf("%p ", currentNode->pointers[currentNode->num_keys]);
		printf("| ");
	}
	printf("\n");
}

int path_to_root(node * const root, node * child) {
	int length = 0;
	node * c = child;
	while (c != root) {
		c = c->parent;
		length++;
	}
	return length;
}

void enqueue(node * new_node) {
	node * c;
	if (queue == NULL) {
		queue = new_node;
		queue->next = NULL;
	}
	else {
		c = queue;
		while(c->next != NULL) {
			c = c->next;
		}
		c->next = new_node;
		new_node->next = NULL;
	}
}

node * dequeue(void) {
	node * n = queue;
	queue = queue->next;
	n->next = NULL;
	return n;
}



//Assume no duplicates, returns pointer to node
node * find_leaf(int key){

	node * currentNode = (node *) hdr->root;

	//Navigate Tree
	while(!(currentNode->is_leaf)){


		//Can replace with binary search
		int i =0;
		while( i < currentNode->num_keys){
			if( key >= currentNode->keys[i]) i++;
			else break;
		} // if key < every other value will follow left most pointerfind
		currentNode = (node *) currentNode->pointers[i];

	}
	//Current Node is leaf
	for(int i = 0; i < currentNode->num_keys; i++){

		if(key == currentNode->keys[i]){
			//We've found the right node
			return currentNode;
		}
	}
	return currentNode;
}

void * find_record(int key){

	node * leafNode = find_leaf(key);
	//redundant work is being done
	for(int i = 0; i < DEFAULT_ORDER; i++){
		if(key == leafNode->keys[i]){
			//We've found the p_pointer 
			return leafNode->pointers[i];
		}
	}
	return NULL;

}

void insert (int key, void * recordPointer){
		node * d_insertNode;
	if(hdr->root == NULL){// if no root create an empty leaf node L, which is also the root
		//Make root if it doesnt exist
		node * rootNode = make_leaf(); 
		hdr->root = rootNode;
		d_insertNode = rootNode;
		
	}
	else {
		d_insertNode = find_leaf(key);
	}

	if (d_insertNode->num_keys < DEFAULT_ORDER - 1){ //There is still space in leaf
		insert_in_leaf(d_insertNode,key,recordPointer);
	}
	else { //leaf is full and must be split
		node * newNode = make_leaf();
		
		int * tempChildKeys = calloc((DEFAULT_ORDER), sizeof(int)); 
		void ** tempChildPointers =  calloc((DEFAULT_ORDER), sizeof(node *));

			//Insert key into temp arrays in order to make sure split occurs correctly
		int i, insertionPoint;
		insertionPoint = 0;
		//TODO
		while(insertionPoint < DEFAULT_ORDER - 1 && d_insertNode->keys[insertionPoint] < key){
			insertionPoint++;
		}
		//Calculate where to split the node into two
		int j;
		for (i = 0, j = 0; i < d_insertNode->num_keys; i++, j++) {
			if (j == insertionPoint) j++;
			tempChildKeys[j] = d_insertNode->keys[i];
			tempChildPointers[j] = d_insertNode->pointers[i];
		}

		tempChildKeys[insertionPoint] = key;
		tempChildPointers[insertionPoint] = recordPointer;

		d_insertNode->num_keys = 0;

		int split = cut(DEFAULT_ORDER);

		//Set leaf rightmost pointers for scanning. P+1 saved for this
		newNode->pointers[DEFAULT_ORDER-1] = d_insertNode->pointers[DEFAULT_ORDER-1];
		d_insertNode->pointers[DEFAULT_ORDER-1] = newNode;

		
		//Erase values in old array
		for(i =0; i < DEFAULT_ORDER; i++){
			d_insertNode->keys[i]= 0;
			d_insertNode->pointers[i] = NULL;			
		}

		// Copy T.K0P0 to TK(n/2)P(n/2) into original node
		for(i =0; i < split; i++){
			d_insertNode->keys[i]= tempChildKeys[i];
			d_insertNode->pointers[i] = tempChildPointers[i];				
			d_insertNode->num_keys++;
		}

		// Copy T.K(n/2)+1P(n/2)+1 to T.KnPn
		for (i = split, j = 0; i < DEFAULT_ORDER; i++, j++){
			newNode->keys[j] = tempChildKeys[i];
			newNode->pointers[j] = tempChildPointers[i];
			newNode->num_keys++;
		}

		free(tempChildKeys);
		free(tempChildPointers);

		//Right most pointer for scan
		d_insertNode->pointers[DEFAULT_ORDER] = newNode;

		newNode->parent = d_insertNode->parent;
		//Promote up
		int newKey = newNode->keys[0];
		insert_in_parent(d_insertNode,newKey,newNode);
	}	
}

/* Finds the appropriate place to
 * split a node that is too big into two.
 */
int cut(int length) {

	if (length % 2 == 0)
		return length/2;
	else
		return length/2 + 1;
}

void insert_in_parent(node * oldNode, int newKey, node * newNode){
	if(oldNode->parent == NULL){
		//Base root case, raises height of tree, oldNode has to be old root
				// printf("%d\n", __LINE__);

		node * newRoot = make_node();

		newRoot->pointers[0] = oldNode;
		newRoot->keys[0] = newKey;
		newRoot->pointers[1] = newNode;
		newRoot->num_keys++;
		newRoot->parent = NULL;
		oldNode->parent = newRoot;
		newNode->parent = newRoot;
		hdr->root = newRoot;
		// printf("Root node:\n");
		// print_node( newRoot);
		// 				printf("%d\n", __LINE__);

		return;
	}
		// printf("%d\n", __LINE__);

	node * oldParent = (node*)oldNode->parent;
	// printf("Parent node:\n");
	// print_node( oldParent);

	// printf(":%d\n", __LINE__);
	//Calculate position of old node in parent node
	int oldNodePos= 0;
	while( oldNodePos <= oldParent->num_keys && 
			((node*)oldParent->pointers[oldNodePos]) != oldNode){
			oldNodePos++;
	}
	assert(oldParent->pointers[oldNodePos] == oldNode);

	// printf(":%d\n", oldNodePos);
	// 	printf("%d\n", __LINE__);


	//Insert into parent node if it fits
	if(oldParent->num_keys < DEFAULT_ORDER-1){
		insert_in_node(oldParent,oldNodePos,newKey,newNode);
	}
	else {
		int * tempKeys = calloc((DEFAULT_ORDER), sizeof(int)); 		
		void ** tempPointers =  calloc((DEFAULT_ORDER+1), sizeof(node *));
		

		//We are copying the Pointers and Keys from the Previous Parent
		//with the new Key to the Temp arrays,OldNodePos holds the index
		//of Key for oldChildNode, OldNodePos+1 points to its pointer

		int h,k,insertKeyPos;
		for(h = 0; h <= DEFAULT_ORDER+1; h++){
			if (h == oldNodePos +1){ // This is were the pointer would be inserted
				tempPointers[h] = newNode;
				// printf("%d,h%d\n", __LINE__,h);
			}
			else if (h <= oldNodePos){ //up to insert
				tempPointers[h] = oldParent->pointers[h];
			}else { //post insert
				tempPointers[h+1] = oldParent->pointers[h];
			}
		}


		for(k = 0; k <= DEFAULT_ORDER; k++){
			if (k == oldNodePos){ // This is were the key would be inserted
				tempKeys[k] = newKey;
				// printf("%d,k%d\n", __LINE__,k);
			}
			else if (k < oldNodePos){ //up to insert
				tempKeys[k] = oldParent->keys[k];
			}else { //post insert
				tempKeys[k+1] = oldParent->keys[k];
			}
		}


		int i,j, insertionPoint;
		//Calculate where to split the node into two
		int split = cut(DEFAULT_ORDER );

		//Make a new parent node
		node * newParent = make_node();
		oldParent->num_keys = 0;
		


		// Copy T.K0P0 to TK(n/2)P(n/2) into original node
		for(i =0; i < split; i++){
			oldParent->keys[i]= tempKeys[i];
			oldParent->pointers[i] = tempPointers[i];				
			oldParent->num_keys++;
		}

		oldParent->pointers[i] = tempPointers[i];		
		int newKey = tempKeys[split];
		// Copy T.K(n/2)+1P(n/2)+1 to T.KnPn
		i += 1; 
		for (j = 0; i < DEFAULT_ORDER; i++, j++){
			newParent->keys[j] = tempKeys[i];
			newParent->pointers[j] = tempPointers[i];
			newParent->num_keys++;
		}


		newParent->pointers[j] = tempPointers[i];
		newParent->parent = oldParent->parent;
		// free(tempKeys);
		// free(tempPointers);
		//Update parent references in children to point to the right one
				

		node * tempNode;
		for (i = 0; i <= newParent->num_keys; i++) {
			tempNode = newParent->pointers[i];
			tempNode->parent = newParent;
		}


		insert_in_parent(oldParent,newKey,newParent);
	}

}


// Should only be called when node is not full
void insert_in_node(node * oldParent, int oldNodePos, int key, node * newNode){

	int i;


	for (i = oldParent->num_keys; i > oldNodePos; i-- ){
		oldParent->keys[i]=oldParent->keys[i-1];
		oldParent->pointers[i+1]=oldParent->pointers[i];
	}
	//This is due to different pointer key arrangement for nodes over leafs
	oldParent->keys[oldNodePos] = key;
	oldParent->pointers[oldNodePos+1] = newNode;
	oldParent->num_keys++;
}

// Should only be called when leaf is not full
void insert_in_leaf(node * leafNode, int key, void * recordPointer){
	printf("%s\n",__func__);

	int i, insertionPoint;

	insertionPoint = 0;

	while(insertionPoint < leafNode->num_keys && leafNode->keys[insertionPoint] < key){
		insertionPoint++;
	}


	for (i = leafNode->num_keys; i > insertionPoint; i-- ){
		leafNode->keys[i]=leafNode->keys[i-1];
		leafNode->pointers[i]=leafNode->pointers[i-1];
	}
	leafNode->keys[insertionPoint] = key;
	leafNode->pointers[insertionPoint] = recordPointer;
	leafNode->num_keys++;
}


void print_node(node * test){
		printf("-----%s-----\n",__func__);
		printf("node: %p\n", test);
		printf("parent: %p\n",test->parent);
		printf("is leaf: %d\n",test->is_leaf);
		printf("num keys: %d\n",test->num_keys);

	for (int i = 0; i < DEFAULT_ORDER; i++) {
		printf("Keys: %d |", test->keys[i]);
		printf("Pointers: %p \n", test->pointers[i]);
	}
	printf("Last Pointer: %p \n", test->pointers[DEFAULT_ORDER]);


}


/* Creates a new record to hold the value
 * to which a key refers.
 */
record * make_record(char name[20],int age, int sal) {

	record * newRecord = (record*) malloc(sizeof(record));
    strcpy(newRecord->name, name);
	newRecord->age = age;
    newRecord->sal = sal;

	void * d_recordPointer;
    appendDatastore_addRecord(newRecord, &d_recordPointer); 

	//Swizzle, take d pointer from storage and make a p pointer
	record * p_recordPointer = twz_ptr_swizzle(bTreeObject, d_recordPointer, FE_READ | FE_WRITE);
	return p_recordPointer;
}


/* Creates a new general node, which can be adapted
 * to serve as either a leaf or an internal node.
 */
node *make_node() {

	void * p_newNode;

	twz_object_init_alloc(bTreeObject, sizeof(node));
	//TODO
	int r = twz_alloc(bTreeObject, sizeof(node), &p_newNode, 0, NULL, NULL);

	//need to lea'd, this is a pointer to an internal struct so should be fine to use
	node *d_newNode = twz_object_lea(bTreeObject, p_newNode);

	for (int i =0 ; i< DEFAULT_ORDER; i++ ){
		d_newNode->keys[i] = 0;
	}
	for ( int i =0 ; i<= DEFAULT_ORDER; i++ ){
		d_newNode->pointers[i] = NULL;
	}
	
	d_newNode->num_keys = 0;
	d_newNode->is_leaf = false;
	d_newNode->num_keys = 0;
	d_newNode->parent = NULL;

	//Increase node count
	hdr->numberOfNodes++;

	return d_newNode;
}

node *make_leaf(){

	node * leaf = make_node();
	leaf->is_leaf = true;
	return leaf;
}
