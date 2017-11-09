#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <iostream>
#include <string>
#include <vector>


using namespace std;

int getmax( int &a, int &b ) {
	if ( a > b ) return a;
	else return b;
}

int main( void ) {

	while ( 1 ) {
		string s;
		cin >> s;
		if( s == "exit" ) break;
		else if ( s == "update" ) {

			string path;
			cin >> path;

			int fdc, fds;
			fdc = open( ("./client/" + path).c_str() , O_RDONLY );
			fds = open( ("./server/" + path).c_str() , O_RDONLY );

			vector<uint64_t> hashc, hashs;

			//read the client file if exist
			if ( fdc ) {

				char buf[1024];
				ssize_t n;
				uint64_t ret = 0;

				while ( ( n = read(fdc, buf, 1024) ) > 0) {
					for (ssize_t i = 0; i < n; ++i) {
					    if( buf[i] == '\n' ) {
					    	hashc.push_back( ret );
					    	ret = 0;
					    }
					    else ret = ret * 131 + buf[i];
					}
				}
				if( ret )  hashc.push_back( ret );

				close(fdc);
			}

			//read the server file if exist
			if ( fds ) {
				char buf[1024];
				ssize_t n;
				uint64_t ret = 0;

				while ( ( n = read(fds, buf, 1024) ) > 0) {
				  for (ssize_t i = 0; i < n; ++i) {
				    if( buf[i] == '\n' ) {
				    	hashs.push_back( ret );
				    	ret = 0;
				    }
				    else ret = ret * 131 + buf[i];
				  }
				}
				if( ret )  hashs.push_back( ret );
				close(fds);
			}


			//get LCS: dpform[j][i]
			int c_size = hashc.size(), s_size = hashs.size();
			int dpform[1000][1000];
			int lcs = 0;

			for( int i=0; i<=c_size; i++ ) dpform[0][i] = 0;
			for( int j=0; j<=s_size; j++ ) dpform[j][0] = 0;

			for( int i=1; i<=c_size; i++ ) {
				for( int j=1; j<=s_size; j++ ) {
					if ( hashc[i-1] == hashs[j-1] )
						dpform[j][i] = dpform[j-1][i-1] + 1;
					else dpform[j][i] = getmax( dpform[j-1][i], dpform[j][i-1] );
					if ( dpform[j][i] > lcs ) lcs = dpform[j][i];
				}	
			}

			//cout << "lcs=" << lcs << " c_size=" << c_size << " s_size=" << s_size << endl;
			cout << c_size-lcs << " " << s_size-lcs << endl;
			cout.flush();

			//check if the file exists and update them
			string cmd;
			if ( fdc == -1 ) 
				cmd = "rm -f ./server/" + path;
			else if( fds == -1 ) 
				cmd = "cp ./client/" + path + " ./server/" + path;
			else
				cmd = "cp ./client/" + path + " ./server/" + path;

			system( cmd.c_str() );

		}
	}

	return 0;
}
