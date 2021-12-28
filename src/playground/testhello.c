/* POSIX includes*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Header files for Memory Access*/
int main(int argc, char **argv){
	//return int
	int c;

	char *input = argv[1];
	FILE *input_file; 

	char templine[40];

	//Open file passed as first argument
	input_file = fopen(input,"r");

	if(input_file == 0){
		// error
		exit(-1);
	}
	else{
		char tconst[9];
		float averageRating;
		int numVotes;
		
		//Discards collumn name
		fgets(templine,40,input_file);

		while((c = fgetc(input_file)) != EOF){
		
		fgets(templine,40,input_file);
		sscanf(templine,"%s %f %d", tconst, &averageRating, &numVotes);


		printf("%s %9.6f %d\n", tconst,averageRating,numVotes);
		}
	}
	return 0;
}

