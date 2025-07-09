#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.h"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("usage: %s [option]\n", argv[0]);
        printf(" options:\n");
        printf("  status         | Gets the card reader status\n");
        printf("  insert [path]  | Inserts a new card at path\n");
        printf("  eject          | Ejects the card\n");
        printf("  version        | Gets the version number of the cardctl program\n");
        return EXIT_SUCCESS;
    }

    if(strcmp(argv[1], "version") == 0) {
        printf("cardctl version %d.%d by Bobby Dilley\n", MAJOR_VERSION, MINOR_VERSION);
        return EXIT_SUCCESS;
    }

    int sockfd, connfd;
    struct sockaddr_in servaddr, cli;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        printf("Error: Failed to open socket");
        return EXIT_FAILURE;
    }

    bzero(&servaddr, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    servaddr.sin_port = htons(PORT);

    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0)
    {
        printf("Cannot connect to cardd on port %d, is it running?\n", PORT);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "status") == 0)
    {
        unsigned char byte = COMMAND_GET_STATUS;
        write(sockfd, &byte, 1);
        read(sockfd, &byte, 1);
        if (byte != COMMAND_SUCCESS)
        {
            printf("Command failed\n");
        }
        read(sockfd, &byte, 1);

        if (byte == COMMAND_STATUS_CARD_EJECTED)
        {
            printf("ejected\n");
        }
        else if (byte == COMMAND_STATUS_CARD_INSERTED)
        {
            printf("inserted\n");
        }

        close(sockfd);
        return EXIT_SUCCESS;
    }

    if (strcmp(argv[1], "insert") == 0)
    {
        if (argc < 3)
        {
            printf("usage: %s insert [path]\n", argv[0]);
            return EXIT_FAILURE;
        }
        unsigned char byte = COMMAND_INSERT_CARD;
        write(sockfd, &byte, 1);
        
        unsigned char length = strlen(argv[2]);
        write(sockfd, &length, 1);

        write(sockfd, argv[2], length);

        read(sockfd, &byte, 1);
        if (byte != COMMAND_SUCCESS)
        {
            printf("Command failed\n");
        }
        printf("card inserted\n");
        close(sockfd);

        return EXIT_SUCCESS;
    }

    if (strcmp(argv[1], "eject") == 0)
    {
        unsigned char byte = COMMAND_EJECT_CARD;
        write(sockfd, &byte, 1);
        read(sockfd, &byte, 1);
        if (byte != COMMAND_SUCCESS)
        {
            printf("Command failed\n");
        }
        printf("card ejected\n");
        close(sockfd);

        return EXIT_SUCCESS;
    }

    printf("Error: Unknown option '%s'\n", argv[1]);

    close(sockfd);

    return EXIT_SUCCESS;
}
