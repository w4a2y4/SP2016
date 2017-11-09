#include <iostream>
#include <string>
#include <vector>
#include <fstream>

using namespace std;

void digin( string prefix ) {

	fstream file;
	string path = "ls -l client/" + prefix + " | awk '{print $1}'  > b049020791.txt";
	system( path.c_str() );
	path = "ls -l client/" + prefix + " | awk '{print $9}' | sed -e 's/\x1b\[[0-9;]*m//g' > b049020792.txt";
	system( path.c_str() );

	vector<string> type, name;

	file.open( "b049020791.txt" , ios::in);
	//string nothing;	file >> nothing;
	while( !file.eof() ) {
		string tmp;
		file >> tmp;
		if( tmp != "總計" ) type.push_back(tmp);
	}
	file.close();

	file.open( "b049020792.txt" , ios::in);
	while( !file.eof() ) {
		string tmp;
		file >> tmp;
		name.push_back(tmp);
	}
	file.close();

	for( int i=0; i<name.size(); i++ ) {
		if( ( type.at(i) )[0] == 'd' && name.at(i)!="" ) {
			string prefix2 = ( prefix + name.at(i) + "/" );
			digin( prefix2 );
		}
		if ( name.at(i) != "" ) cout << prefix << name.at(i) << endl;
	}

	system("rm -f b049020791.txt b049020792.txt");
	return;
}

int main () {

	system("cp -R client/* server/");
	digin("");

	return 0;
}