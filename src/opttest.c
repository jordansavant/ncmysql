#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int aflag=0, bflag=0;
    char *cval = NULL;
    int c;

    opterr = 0; // hide error output
    while ((c = getopt(argc, argv, "abc:")) != -1) {
        switch (c) {
            case 'a':
                aflag = 1;
                break;
            case 'b':
                bflag = 1;
                break;
            case 'c':
                cval = optarg;
                break;
            case '?': // appears if unknown option when opterr=0
                if (optopt == 'c')
                    fprintf(stderr, "-c option requires argument\n");
                else if (isprint(optopt))
                    fprintf(stderr, "unknown option '-%c'\n", c);
                else
                    fprintf(stderr, "unknown option '\\x%x'\n", optopt);
                return 1;
            default:
                fprintf(stderr, "unknown getopt error");
                return 1;
        }
    }

    printf("aflag = %d, bflag = %d, cvalue = %s\n", aflag, bflag, cval);

    for (int index = optind; index < argc; index++)
        printf("Non-option argument %s\n", argv[index]);

    return 0;
}
