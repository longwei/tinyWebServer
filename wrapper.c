#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h> /* for pid_t */
#include <sys/wait.h> /* for wait */

int main(int argc, char *argv[]) {
  FILE *fp;
  int status;
  char output[100];

  fp = popen("QUERY_STRING='m=4&n=9' python mult.py", "r");
  if (fp == NULL){
  	perror ("Error opening file");
  } else {
  	if ( fgets (output , 100 , fp) != NULL ){
  		puts(output);
  	}
  	fclose(fp);
  }
  return 0;
}