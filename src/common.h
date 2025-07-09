/* Commands to control the emulator daemon */
#define COMMAND_GET_STATUS 1
#define COMMAND_INSERT_CARD 2
#define COMMAND_EJECT_CARD 3

/* Statuses of the card */
#define COMMAND_STATUS_CARD_INSERTED 1
#define COMMAND_STATUS_CARD_EJECTED 0

/* Response commands */
#define COMMAND_SUCCESS 0
#define COMMAND_FAILURE 255

/* The port to control the emulator */
#define PORT 2000

/* Version number */
#define MAJOR_VERSION 0
#define MINOR_VERSION 3