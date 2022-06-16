#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    printf("Usage: %s [option]\n", argv[0]);
    printf("  Options:\n");
    printf("    --status       | Gets the card reader status\n");
    printf("    --card [path]  | Inserts a new card\n");
    printf("    --eject        | Ejects the card\n");
    
    return EXIT_SUCCESS;
}
