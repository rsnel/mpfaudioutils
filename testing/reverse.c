#include <stdlib.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
	int in, out;

	while ((in = fgetc(stdin)) != EOF) {
		out = 0xff - in;
		fputc(out, stdout);
	}
	
	exit(0);
}
