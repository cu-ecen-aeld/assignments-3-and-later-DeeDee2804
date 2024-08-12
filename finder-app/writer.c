#include <syslog.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    openlog(NULL, 0, LOG_USER);
    
    
    if (argc != 3) {
        syslog(LOG_ERR, "Number of argument of must be 2. Ex: %s writefile writestring", argv[0]);
        return 1;
    }
    
    char* writefile = argv[1];
    char* writestr = argv[2];

    FILE *fptr = fopen(writefile, "w");
    if (fptr == NULL) {
        syslog(LOG_ERR, "Error to opening file %s", writefile);
        return 1;
    }
    else {
        fputs(writestr, fptr);
        syslog(LOG_DEBUG, "Write %s to %s", writestr, writefile);
        return 0;
    }
}