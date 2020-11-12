#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    int prc = fork();
    if (prc < 0) {
        printf("error creating child\n");
        return 1;
    }

    if (prc) {
        // child
        printf("in child\n");
        // I want to use this:
        // ssh -f -L :2222:sts.c0s9xnf5ze2m.us-east-1.rds.amazonaws.com:3306 grimoire sleep 5
        // because it opens the connection with a window to wait until our prcess connects
        // if we connect then it remains open, if we fail it closes after the 5 second window
        //system("ssh -L :2222:sts.c0s9xnf5ze2m.us-east-1.rds.amazonaws.com:3306 grimoire");
        system("ssh -f -L :2222:sts.c0s9xnf5ze2m.us-east-1.rds.amazonaws.com:3306 grimoire sleep 5");
    } else {
        // parent
        printf("in parent\n");
    }
}
