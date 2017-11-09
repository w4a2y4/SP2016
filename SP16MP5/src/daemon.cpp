int main(int argc, char* argv[]) {

	FILE *fp= NULL;
	pid_t process_id = 0;
	pid_t sid = 0;

	// Create child process
	process_id = fork();
	// Indication of fork() failure
	if (process_id < 0) {
	 printf("fork failed!\n");
	 // Return failure in exit status
	 exit(1);
	}
	
	// PARENT PROCESS. Need to kill it.
	if (process_id > 0) {
	 printf("process_id of child process %d \n", process_id);
	 // return success in exit status
	 exit(0);
	}

	//unmask the file mode
	umask(0);
	//set new session
	sid = setsid();
	if(sid < 0) {
	 // Return failure
		exit(1); 
	}
	// Change the current working directory to root, required root permission, or tmp
	if ( chdir("/tmp") < 0 )
		fprintf(STDERR_FILENO, "Cannot switch to tmp directory");
	/*
	* Attach file descriptors 0, 1, and 2 to /dev/null.
	*/
	fd0 = open("/dev/null", O_RDWR); //STDIN
	fd1 = dup(0); // STDOUT
	fd2 = dup(0); // STDERR

	// Open a log file in write mode.
	fp = fopen ("Log.txt", "w+");
	while (1) {
		//Dont block context switches, let the process sleep for some time
		sleep(1);
		fprintf(fp, "Logging info...\n");
		fflush(fp);
		// Start your dropbox client and server here.
	}
	fclose(fp);
	return (0); 
}








