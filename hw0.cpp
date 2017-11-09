#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

bool is_vowel ( char& c ) {
	if ( c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'
	  	|| c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' )
			return true;
	else return false;
}

int main( int argc, char *argv[] ) {	// [1]in [2]out

	int fdin, fdout, cnt=0;
	fdin = open( argv[1], O_RDONLY );
	fdout = open( argv[2], O_WRONLY | O_CREAT , 0644);

	char buf, s[10];
	while( read( fdin, &buf, sizeof(char) ) ) {
		//printf("%d %c ", cnt, buf);
		if ( is_vowel( buf ) ) 
			cnt++;
	}
	close( fdin );
	sprintf( s, "%d", cnt );
	write( fdout, &s, sizeof(s) );
	close( fdout );


	return 0;
}
