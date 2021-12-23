/* POSIX includes*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* access to the twizzler object API */
#include <twz/alloc.h>
#include <twz/meta.h>
#include <twz/name.h>
#include <twz/obj.h>
#include <twz/ptr.h>

/* Header files for Memory Access*/
int main(int argc, char **argv){

	printf("Hello, World!\n");

	//return int
	int c;

	char *input = argv[1];
	FILE *input_file; 

	char* templine[90];

	//Open file passwed as first argument
	input_file = fopen(input)

	if(input_file == 0){
		// error
		exit(-1);
	}
	else{
		while((c = fgetc(input_file)) != EOF){
		

		fgets(,,)

		}
	}




	return 0;
}
