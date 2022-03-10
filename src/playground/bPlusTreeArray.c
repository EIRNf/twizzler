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

#include "arrayStore.h"

/* Macros */
//331
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
int find_record(int key);
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
    // appendDatastore_init(&datao);
	arrayDatastore_init(&datao);

	twzobj dataObj; 
	tree_init(&dataObj);

	//make hdr if it doesnt exist
	hdr = twz_object_base(bTreeObject);

	bulk_generation();

	arrayDatastore_printRecord(find_record(2));
	arrayDatastore_printRecord(find_record(6));
	arrayDatastore_printRecord(find_record(7));
	arrayDatastore_printRecord(find_record(8));



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
			// 			printf("_____________________________________________________\n");

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

	//Future experiments
		//Index extension
		//Storage extension


	// printf("%d\n", __LINE__);
	// for(int i = 0; i < numElements; i++){
	// 	//int k = i | (int) 0 << 16;
	// 	// printf("%d\n", __LINE__);

	// 	print_node(find_leaf(i));
	// }


	// //make full leaf node
	// node * tempFullLeaf = make_leaf();

	// int maxNodeSize = DEFAULT_ORDER ;

	// int incrementId = 0;
	// //make max records for leaf node
	// int i;
	// for(i = 0; i < maxNodeSize; i++){
	// 	int k = i | (int)incrementId << 8;
	// 	tempFullLeaf->keys[i] = k;  //Bit
	// 	tempFullLeaf->pointers[i] = (void*) make_record("tempName",k,k*k); // dummy record to check secondary indexing
	// }
	// tempFullLeaf->num_keys = i;

	// print_node(tempFullLeaf);
	// //split if reached max size
	// rootNode->keys[incrementId] = tempFullLeaf->keys[maxNodeSize - 1];
	// rootNode->pointers[incrementId] = tempFullLeaf;
		
	// incrementId++;

	//make parent, give it pointers to two new children, parent is root, propagate slpite up tree

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
	// printf("%s\n",__func__);
	// printf("%d\n",key);


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

		// for(int i = 0; i < currentNode->num_keys; i++){
		// 	 if (key < currentNode->keys[i]){
		// 		//Only applies for navigate nodes, otherwise p_pointers screw things up
		// 		currentNode = currentNode->pointers[i];
		// 	 }
		// 	 else if (key == currentNode->keys[i]){
		// 		currentNode = currentNode->pointers[i+1];
		// 	 }
		// 	 else {

		// 	 }
		// }
	}
	//Current Node is leaf
	for(int i = 0; i < currentNode->num_keys; i++){

		if(key == currentNode->keys[i]){
			//We've found the right node
			return currentNode;
		}
	}
	// printf("currentNode:\n");
	// print_node(currentNode);

	return currentNode;
}

int find_record(int key){

	node * leafNode = find_leaf(key);
	//redundant work is being done
	for(int i = 0; i < DEFAULT_ORDER; i++){
		if(key == leafNode->keys[i]){
			//We've found the p_pointer 
			//return (void *) twz_object_lea(bTreeObject, leafNode->pointers[i]);
			//printf("pointer: %p");
			return leafNode->keys[i];
		}
	}
	return 0;

}

void insert (int key, void * recordPointer){
	// printf("%s\n",__func__);

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

		//Copy over keys and pointers to temp location
		// memcpy(tempKeys,d_insertNode->keys, DEFAULT_ORDER +1 );
		// memcpy(tempPointers,d_insertNode->pointers, DEFAULT_ORDER+1);

		//Insert key into temp arrays in order to make sure split occurs correctly
		int i, insertionPoint;
		insertionPoint = 0;
		//TODO
		while(insertionPoint < DEFAULT_ORDER - 1 && d_insertNode->keys[insertionPoint] < key){
			insertionPoint++;
		}
		int j;
		for (i = 0, j = 0; i < d_insertNode->num_keys; i++, j++) {
			if (j == insertionPoint) j++;
			tempChildKeys[j] = d_insertNode->keys[i];
			tempChildPointers[j] = d_insertNode->pointers[i];
		}

		tempChildKeys[insertionPoint] = key;
		tempChildPointers[insertionPoint] = recordPointer;

		// d_insertNode->keys[insertionPoint] = key;
		// d_insertNode->pointers[insertionPoint] = recordPointer;
		d_insertNode->num_keys = 0;

		int split = cut(DEFAULT_ORDER);

		//Set leaf rightmost pointers for scanning. P+1 saved for this
		newNode->pointers[DEFAULT_ORDER-1] = d_insertNode->pointers[DEFAULT_ORDER-1];
		d_insertNode->pointers[DEFAULT_ORDER-1] = newNode;

		//Calculate where to split the node into two

		// printf("split: %d\n", split);
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

				// printf("%d\n", __LINE__);


		free(tempChildKeys);
		free(tempChildPointers);

		//Right most pointer for scan
		d_insertNode->pointers[DEFAULT_ORDER] = newNode;
					// printf("%d\n", __LINE__);


		newNode->parent = d_insertNode->parent;
		//Promote up
		int newKey = newNode->keys[0];
		// printf("newKey: %d", newKey);
		insert_in_parent(d_insertNode,newKey,newNode);
	}	
}

/* Finds the appropriate place to
 * split a node that is too big into two.
 */
int cut(int length) {
	// printf("%s\n",__func__);


	if (length % 2 == 0)
		return length/2;
	else
		return length/2 + 1;
}

void insert_in_parent(node * oldNode, int newKey, node * newNode){
		printf("%s\n",__func__);

		// printf("%s\n",__func__);
		// printf("%d\n", __LINE__);

	// printf("Old node:\n");
	// print_node(oldNode);
	// printf("New node:\n");
	// print_node(newNode);
	// printf("Root node:\n");
	// print_node( hdr->root );
	// 	printf("%d\n", __LINE__);

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
		

		printf("%s\n", "inserting into parent");

		int * tempKeys = calloc((DEFAULT_ORDER), sizeof(int)); 
		void ** tempPointers =  calloc((DEFAULT_ORDER+1), sizeof(node *));
		// printf("%d\n", __LINE__);


		// for(i = 0; i < DEFAULT_ORDER;i++){
		// 	printf("tempkeys: %d, %d\n", i,tempKeys[i]);
		// }
		// for(i = 0; i < DEFAULT_ORDER+1;i++){
		// 	printf("tempPointers: %d, %p\n", i,tempPointers[i]);
		// }
 

		//Copy over keys and pointers to temp location
		// memcpy(tempKeys,oldNode->keys, DEFAULT_ORDER );
		// memcpy(tempPointers,oldNode->pointers, DEFAULT_ORDER+1);

		//Insert key into temp arrays in order to make sure split occurs correctly
		//Insert rigth after 
		// int i,j, insertionPoint;
		// insertionPoint = 0;
		// while(insertionPoint < DEFAULT_ORDER - 1  && oldNode->keys[insertionPoint] < oldNodePos){
		// 	insertionPoint++;
		// }
		// for (i = insertionPoint; i < DEFAULT_ORDER; i++ ){
		// 	tempKeys[i+1]=oldNode->keys[i];
		// 	tempPointers[i+1]=oldNode->pointers[i];
		// }

		//We are copying the Pointers and Keys from the Previous Parent
		//with the new Key to the Temp arrays,OldNodePos holds the index
		//of Key for oldChildNode, OldNodePos+1 points to its pointer
		// printf("oldNodePos:%d,newkey:%d\n",oldNodePos,newKey);
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
		// tempKeys[insertionPoint] = newKey;
		// tempPointers[insertionPoint+1] = newNode;


		int i,j, insertionPoint;
		// printf("j:%d,oldNodePos:%d\n", j, oldNodePos);
		// for (i = 0, j = 0; i < oldParent->num_keys + 1; i++, j++) {
		// if (j == oldNodePos + 1) j +=1 ;
		// 	// printf("j:%d,oldNodePos:%d\n", j, oldNodePos);
		// 	printf("j:%d\n", j);
		// 	tempPointers[j] = oldParent->pointers[i];
		// }
		// 				printf("%d\n", __LINE__);
		// for (i = 0, j = 0; i < oldParent->num_keys; i++, j++) {
		// 	if (j == oldNodePos + 1) {
		// 		// printf("j:%d,oldNodePos:%d\n", j, oldNodePos);
		// 		printf("j:%d\n", j);
		// 		insertionPoint = j;
		// 		j += 1;
		// 	} 
		// //printf("j:%d\n", j);
		// tempKeys[j] = oldParent->keys[i];
	    // }
		// 				printf("%d\n", __LINE__);
// printf("%d\n", __LINE__);

		//printf("j:%d\n", j);
		// printf("insertion point: %d\n", insertionPoint);
		// tempKeys[insertionPoint] = newKey;
		// tempPointers[insertionPoint+1] = newNode;

		//Calculate where to split the node into two
		int split = cut(DEFAULT_ORDER );

		//Erase values in old array
		// for(i =0; i < DEFAULT_ORDER-1; i++){
		// 	oldNode->keys[i]= 0;
		// 	oldNode->pointers[i] = NULL;			
		// }
// printf("%d\n", __LINE__);
		//Make a new parent node
		node * newParent = make_node();
		oldParent->num_keys = 0;

		// for(i = 0; i < DEFAULT_ORDER;i++){
		// 	printf("tempkeys: %d, %d\n", i,tempKeys[i]);
		// }
		// for(i = 0; i < DEFAULT_ORDER+1 ;i++){
		// 	printf("tempPointers: %d, %p\n", i,tempPointers[i]);
		// }
// printf("%d\n", __LINE__);
		// Copy T.K0P0 to TK(n/2)P(n/2) into original node
		for(i =0; i < split; i++){
			oldParent->keys[i]= tempKeys[i];
			oldParent->pointers[i] = tempPointers[i];				
			oldParent->num_keys++;
		}
		oldParent->pointers[i] = tempPointers[i];		
		int newKey = tempKeys[split];
// printf("%d\n", __LINE__);
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
			// printf("tempNode node:\n");
			// print_node( tempNode);
			tempNode->parent = newParent;
		}

		//Promote up
		insert_in_parent(oldParent,newKey,newParent);
	}

}


// Should only be called when node is not full
void insert_in_node(node * oldParent, int oldNodePos, int key, node * newNode){

	int i;

	// insertionPoint = 0;

	// // while(insertionPoint < oldParent->num_keys && leafNode->keys[insertionPoint] < key){
	// // 	insertionPoint++;
	// // }


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

	void * d_recordPointer = NULL;
    // appendDatastore_addRecord(newRecord, &d_recordPointer); 

	arrayDatastore_addRecord(newRecord,age);

	//Swizzle, take d pointer from storage and make a p pointer
	// record * p_recordPointer = twz_ptr_swizzle(bTreeObject, d_recordPointer, FE_READ | FE_WRITE);
	return d_recordPointer;
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
	
	// d_newNode->keys[0]; // Keys array, no dynamic allocation
	// d_newNode->pointers[0];
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