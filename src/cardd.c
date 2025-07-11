#include <fcntl.h>
#include <linux/serial.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

#define TIMEOUT_SELECT 1000
#define BUFFER_SIZE 1024

unsigned char inputBuffer[BUFFER_SIZE];
unsigned char outputBuffer[BUFFER_SIZE];

int serialIO = -1;

/* Card Status Definitions */
#define STATUS_NO_CARD 0x30
#define STATUS_HAS_CARD_1 0x31
#define STATUS_CARD_ERROR 0x32
#define STATUS_HAS_CARD_2 0x33
#define STATUS_EJECTING_CARD 0x34

/* Reader Status Definitions */
#define STATUS_NO_ERR 0x30
#define STATUS_READ_ERR 0x31
#define STATUS_WRITE_ERR 0x32
#define STATUS_CARD_JAM 0x33
#define STATUS_MOTOR_ERR 0x34
#define STATUS_PRINT_ERR 0x35
#define STATUS_ILLEGAL_ERR 0x38
#define STATUS_BATTERY_ERR 0x40
#define STATUS_SYSTEM_ERR 0x41
#define STATUS_TRACK_1_READ_ERR 0x51
#define STATUS_TRACK_2_READ_ERR 0x52
#define STATUS_TRACK_3_READ_ERR 0x53
#define STATUS_TRACK_1_AND_2_READ_ERR 0x54
#define STATUS_TRACK_1_AND_3_READ_ERR 0x55
#define STATUS_TRACK_2_AND_3_READ_ERR 0x56

/* Job Status Definitions */
#define STATUS_NO_JOB 0x30
#define STATUS_ILLEGAL_COMMAND 0x32
#define STATUS_RUNNING_COMMAND 0x33
#define STATUS_WAITING_FOR_CARD 0x34
#define STATUS_DISPENSER_EMPTY 0x35
#define STATUS_NO_DISPENSER 0x36
#define STATUS_CARD_FULL 0x37

/* Protocol Symbolic Bytes */
#define START_OF_TEXT 0x02
#define END_OF_TEXT 0x03
#define ENQUIRY 0x05
#define ACK 0x06

/* Command Bytes */
#define INIT 0x10
#define REGISTER_FONT 0x7A
#define GET_STATUS 0x20
#define SET_SHUTTER 0xD0
#define CLEAN_CARD 0xA0
#define EJECT_CARD 0x80
#define READ 0x33
#define WRITE 0x53
#define ERASE 0x7D
#define PRINT 0x7C
#define SET_PRINT_PARAM 0x78
#define NEW_CARD 0xB0
#define CANCEL 0x40

/* Data sizes */
#define TRACK_SIZE 69

/* Default Paths */
#define DEFAULT_SERIAL_PATH "/dev/ttyUSB0"

typedef enum
{
	DERBY_OWNERS_CLUB,
	DERBY_OWNERS_CLUB_RS232,
	WANGAN_MIDNIGHT_MAXIMUM_TUNE_3,
	F_ZERO_AX,
	F_ZERO_AX_MONSTER_RIDE,
	MARIO_KART_ARCADE_GP,
	MARIO_KART_ARCADE_GP_2,
	INITIAL_D,
} Game;

typedef enum
{
	NOT_INSERTED,
	INSERTED_IN_FRONT,
	UNDER_PRINT_HEAD,
	UNDER_READER,
	DISPENCING_FROM_BACK,
	EJECTING_CARD,
} CardPosition;

typedef struct
{
	CardPosition cardPosition;
	int dispenserFull;
	int coverClosed;
	unsigned char readerStatus;
	unsigned char jobStatus;
} CardReader;

typedef struct
{
	int fd;
	int *running;
} RS422ThreadArguments;

typedef struct
{
	CardReader *reader;
} ControlThreadArguments;

typedef struct
{
	unsigned char buffer[BUFFER_SIZE];
	int head;
	int tail;
} CircularBuffer;

CircularBuffer rs422InputBuffer;
CircularBuffer rs422OutputBuffer;

char cardPath[256];

unsigned char tracks[3][TRACK_SIZE];

void saveCardToFile() {
    FILE *file = fopen(cardPath, "wb");

    if (file == NULL) {
        perror("Error: Couldn't open file for writing");
        return;
    }

    size_t written = fwrite(tracks, sizeof(unsigned char), 3 * TRACK_SIZE, file);
    if (written != 3 * TRACK_SIZE) {
        printf("Error : Couldn't write tracks to file");
    }

    fclose(file);
}

void loadCardFromFile() {
    FILE *file = fopen(cardPath, "rb");
    if (!file) {
        printf("Error: Failed to open file for reading, creating a new card.\n");

		for (int i = 0; i < 3; i++)
				for (int j = 0; j < TRACK_SIZE; j++)
					tracks[i][j] = 0x00;

        return saveCardToFile();
    }

    size_t read = fread(tracks, sizeof(unsigned char), 3 * TRACK_SIZE, file);
    if (read != 3 * TRACK_SIZE) {
        printf("Error: Failed to read complete data from file");
    }

    fclose(file);
}

/**
 * Generates card status based upon card struct for both status modes
 *
 * Depending on the reader, there are two forms of status bytes that might
 * be required. This function will transparently generate both of those by
 * taking physical attributes from the reader reader struct.
 *
 * @param reader The card reader struct to take attributes from
 * @param shutterMode True if the reader contains a shutter
 * @returns The status byte
 **/
char getCardStatus(CardReader *reader, int shutterMode)
{
	if (!shutterMode)
	{
		switch (reader->cardPosition)
		{
		case NOT_INSERTED:
			return STATUS_NO_CARD;
			break;
		case INSERTED_IN_FRONT:
		case UNDER_PRINT_HEAD:
		case UNDER_READER:
		case DISPENCING_FROM_BACK:
			return STATUS_HAS_CARD_1;
			break;
		case EJECTING_CARD:
			return STATUS_EJECTING_CARD;
			break;
		}
	}

	// Shutters
	char result = 1 << ((reader->coverClosed & 0x01) ? 7 : 6);

	// Dispenser Full
	result |= (reader->dispenserFull & 0x01) << 5;

	// Card position
	switch (reader->cardPosition)
	{
	case NOT_INSERTED:
		break;
	case INSERTED_IN_FRONT:
		result |= 0b00001;
		break;
	case UNDER_PRINT_HEAD:
		result |= 0b00111;
		break;
	case UNDER_READER:
		result |= 0b11000;
		break;
	case DISPENCING_FROM_BACK:
		result |= 0b11100;
		break;
	case EJECTING_CARD:
		break;
	}

	return result;
}

int setSerialAttributes(int fd, int myBaud, int parity, int flow)
{
	struct termios options;
	int status;
	tcgetattr(fd, &options);

	cfmakeraw(&options);
	cfsetispeed(&options, myBaud);
	cfsetospeed(&options, myBaud);

	options.c_cflag |= (CLOCAL | CREAD);
	options.c_cflag &= ~PARENB; // Disable Pairty Bit (EVEN)
	options.c_cflag &= ~CSTOPB; // Set one stop bit
	options.c_cflag &= ~CSIZE;
	options.c_cflag |= CS8; // 8 BIT SIZE
	options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	options.c_oflag &= ~OPOST;

	// SET EVEN PARITY
	if (parity)
	{
		options.c_cflag |= PARENB;
		options.c_cflag &= ~PARODD;
	}

	// ENABLE FLOW CONTROL
	if (flow)
	{
		options.c_cflag |= CRTSCTS;
	}

	// SET INTER BYTE TIMEOUTS TO 0
	options.c_cc[VMIN] = 0;
	options.c_cc[VTIME] = 0;

	// SET OPTIONS
	tcsetattr(fd, TCSANOW, &options);

	/*
	ioctl(fd, TIOCMGET, &status);

	status |= TIOCM_DTR;
	status |= TIOCM_RTS;

	ioctl(fd, TIOCMSET, &status);
*/
	usleep(100 * 1000); // 10mS

	struct serial_struct serial_settings;

	ioctl(fd, TIOCGSERIAL, &serial_settings);

	serial_settings.flags |= ASYNC_LOW_LATENCY;
	ioctl(fd, TIOCSSERIAL, &serial_settings);

	tcflush(fd, TCIOFLUSH);
	usleep(100 * 1000);

	return 0;
}

int readBytes(unsigned char *buffer, int amount, int rs422Mode)
{
	if (rs422Mode)
	{
		if (rs422InputBuffer.head == rs422InputBuffer.tail)
		{
			usleep(TIMEOUT_SELECT * 1000);
			return 0;
		}

		int size = BUFFER_SIZE + rs422InputBuffer.head - rs422InputBuffer.tail;
		if (rs422InputBuffer.head >= rs422InputBuffer.tail)
			size = rs422InputBuffer.head - rs422InputBuffer.tail;

		memcpy(buffer, &rs422InputBuffer.buffer[rs422InputBuffer.tail], size);

		if (rs422InputBuffer.tail + size > BUFFER_SIZE)
			rs422InputBuffer.tail = rs422InputBuffer.tail + size - BUFFER_SIZE;
		else
			rs422InputBuffer.tail += size;

		return size;
	}

	fd_set fd_serial;
	struct timeval tv;

	FD_ZERO(&fd_serial);
	FD_SET(serialIO, &fd_serial);

	tv.tv_sec = 0;
	tv.tv_usec = TIMEOUT_SELECT * 1000;

	int filesReadyToRead = select(serialIO + 1, &fd_serial, NULL, NULL, &tv);

	if (filesReadyToRead < 1)
		return -1;

	if (!FD_ISSET(serialIO, &fd_serial))
		return -1;

	return read(serialIO, buffer, amount);
}

int writeBytes(unsigned char *buffer, int amount, int rs422Mode)
{
	if (amount < 1)
		return 0;

	if (rs422Mode)
	{
		if (rs422OutputBuffer.head + 1 == rs422OutputBuffer.tail)
			return 0;

		if (rs422OutputBuffer.head + amount > BUFFER_SIZE)
		{
			int firstPart = BUFFER_SIZE - rs422OutputBuffer.head;
			int secondPart = amount - firstPart;
			memcpy(&rs422OutputBuffer.buffer[rs422OutputBuffer.head], buffer, firstPart);
			memcpy(&rs422OutputBuffer.buffer[0], buffer + firstPart, secondPart);
			rs422OutputBuffer.head = secondPart;
		}
		else
		{
			memcpy(&rs422OutputBuffer.buffer[rs422OutputBuffer.head], buffer, amount);
			rs422OutputBuffer.head += amount;
		}

		return amount;
	}

	/*printf("writeBytes: ");
	for (int i = 0; i < amount; i++)
	{
		printf("%X ", buffer[i]);
	}
	printf("\n");*/

	return write(serialIO, buffer, amount);
}

int closeDevice(int fd)
{
	tcflush(fd, TCIOFLUSH);
	return close(fd) == 0;
}

int writePacket(unsigned char *packet, int length, int rs422Mode)
{
	unsigned char outputPacket[BUFFER_SIZE];

	int index = 0;

	outputPacket[index++] = START_OF_TEXT;

	outputPacket[index++] = length + 2;
	unsigned char checksum = outputPacket[index - 1];

	for (int i = 0; i < length; i++)
	{
		outputPacket[index++] = packet[i];
		checksum ^= packet[i];
	}

	outputPacket[index++] = END_OF_TEXT;
	checksum ^= outputPacket[index - 1];

	outputPacket[index++] = checksum;

	writeBytes(outputPacket, (length + 4), rs422Mode);
}

/**
 * Read in a card reader packet
 *
 * Reads in a packet from the card reader taking into account if we're running
 * in RS422 mode and require to skip the address bytes that we get before each
 * data byte.
 *
 * @param packet The address of the packet buffer to fill with the read packet
 * @param rs422Mode Set if reader is connected directly to the Naomi RS422 pins
 * @returns The length of the packet read
 * */
int readPacket(unsigned char *packet, int rs422Mode)
{
	int bytesAvailable = 0, phase = 0, index = 0, dataIndex = 0, finished = 0;
	unsigned char checksum = 0x00;
	unsigned char length;

	while (!finished)
	{
		int bytesRead = readBytes(inputBuffer + bytesAvailable, BUFFER_SIZE - bytesAvailable, rs422Mode);

		if (bytesRead < 0)
			return -1;

		bytesAvailable += bytesRead;

		while ((index < bytesAvailable) && !finished)
		{
			switch (phase)
			{
			case 0:
				if (inputBuffer[index] == ENQUIRY)
				{
					packet[0] = inputBuffer[index];
					return 1;
				}
				else if (inputBuffer[index] == START_OF_TEXT)
				{
					phase++;
				}
				break;
			case 1:
				length = inputBuffer[index];
				checksum = length;
				phase++;
				break;
			case 2:
				if (dataIndex == length - 2)
					phase++;

				packet[dataIndex++] = inputBuffer[index];
				checksum ^= inputBuffer[index];
				break;
			case 3:
				if (checksum != inputBuffer[index])
				{
					printf("Error: The checksums did not match.\n");
					return -1;
				}
				finished = 1;
				break;
			default:
				break;
			}
			index += 1;
		}
	}

	return length - 2;
}

void getTrackIndex(unsigned char track, int *trackIndex)
{
	trackIndex[0] = -1;
	trackIndex[1] = -1;
	trackIndex[2] = -1;

	switch (track)
	{
	case 0x30:
	case 0x31:
	case 0x32:
		trackIndex[0] = track - 0x30;
		break;
	case 0x33:
		trackIndex[0] = 0;
		trackIndex[1] = 1;
		break;
	case 0x34:
		trackIndex[0] = 0;
		trackIndex[1] = 2;
		break;
	case 0x35:
		trackIndex[0] = 1;
		trackIndex[1] = 2;
		break;
	case 0x36:
		trackIndex[0] = 0;
		trackIndex[1] = 1;
		trackIndex[2] = 2;
		break;
	default:
		printf("Error: No track exists\n");
		break;
	}
}

/**
 * Listens for control commands from cardctl
 *
 * This is a TCP control thread that listens for commands
 * from cardctl. When a command is received, it is parsed
 * and the appropriate action is taken such as insert/eject.
 */
void *controlThread(void *vargp)
{
	ControlThreadArguments *arguments = (ControlThreadArguments *)vargp;

	int new_socket, opt = 1;
	struct sockaddr_in address;
	int addrlen = sizeof(address);

	int server_fd = socket(AF_INET, SOCK_STREAM, 0);

	if (server_fd == 0)
	{
		printf("Error: Failed to create socket\n");
		return 0;
	}

	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
	{
		printf("Error: Failed to set socket options\n");
		return 0;
	}

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(PORT);

	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
	{
		printf("Error: Failed to bind socket\n");
		return 0;
	}

	if (listen(server_fd, 3) < 0)
	{
		printf("Error: Listen failed");
		return 0;
	}

	while (1)
	{
		if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
		{
			printf("Error: Failed to accept\n");
			return 0;
		}

		unsigned char control = 0;
		int bytesRead = read(new_socket, &control, 1);

		if (bytesRead < 1)
		{
			close(new_socket);
			continue;
		}

		unsigned char response = COMMAND_SUCCESS;

		unsigned char responseBuffer[BUFFER_SIZE];
		unsigned char responseLength = 0;

		switch (control)
		{
		case COMMAND_GET_STATUS:
		{
			printf("COMMAND GET STATUS %d\n", arguments->reader->cardPosition);
			if (arguments->reader->cardPosition != NOT_INSERTED)
			{
				responseBuffer[responseLength++] = COMMAND_STATUS_CARD_INSERTED;
			} else {
				responseBuffer[responseLength++] = COMMAND_STATUS_CARD_EJECTED;
			}
		}

		break;

		case COMMAND_INSERT_CARD:
		{
			printf("COMMAND INSERT CARD\n");
			unsigned char length = 0;
			bytesRead = read(new_socket, &length, 1);

			if(bytesRead != 1) {
				printf("NO PATH ERROR\n");
				response = COMMAND_FAILURE;
				break;
			}

			unsigned char filePath[256] = {0};
			read(new_socket, &filePath, length);
			

			strcpy(cardPath, filePath);
			printf("File path updated %s\n", cardPath);

			loadCardFromFile();

			arguments->reader->cardPosition = INSERTED_IN_FRONT;
		}
		break;
			
		case COMMAND_EJECT_CARD:
			printf("COMMAND EJECT CARD\n");
			arguments->reader->cardPosition = NOT_INSERTED;
			break;

		default:
			printf("UNKNOWN CONTROL COMMAND\n");
			response = COMMAND_FAILURE;
			break;
		}

		write(new_socket, &response, 1);

		if(responseLength != 0) {
			write(new_socket, &responseBuffer, responseLength);
		}
	}

	return 0;
}

/**
 * This thread deals with the RS422 ring network communication
 *
 * Derby Owners Club uses a conversion board that speaks RS422 and
 * converts to RS232. This thread will act as that conversion board
 * and fill the input/output buffer with correct packets.
 **/
void *rs422Thread(void *vargp)
{
	RS422ThreadArguments *arguments = (RS422ThreadArguments *)vargp;

	rs422InputBuffer.head = rs422OutputBuffer.head = 0;
	rs422InputBuffer.tail = rs422OutputBuffer.tail = 0;

	while (arguments->running)
	{
		unsigned char buffer[2];

		int bytesLeft = 2;

		int bytesRead = 0;

		while (bytesLeft > 0)
		{
			bytesRead = readBytes(buffer + (2 - bytesLeft), bytesLeft, 0);
			if (bytesRead < 1)
				continue;
			bytesLeft -= bytesRead;
		}

		switch (buffer[0])
		{
		case 0x01:
		{
			writeBytes(buffer, 2, 0);

			if (rs422InputBuffer.head + 1 == rs422InputBuffer.tail)
			{
				printf("Error: Buffer full\n");
				arguments->running = 0;
				continue;
			}

			rs422InputBuffer.buffer[rs422InputBuffer.head] = buffer[1];

			if (rs422InputBuffer.head + 1 == BUFFER_SIZE)
				rs422InputBuffer.head = 0;
			else
				rs422InputBuffer.head++;
		}
		break;

		case 0x80:
		{
			unsigned char outputBuffer[2] = {0x80, 0x00}; // Empty
			if (rs422OutputBuffer.head != rs422OutputBuffer.tail)
			{
				outputBuffer[1] = 0x40; // Not empty
			}
			writeBytes(outputBuffer, 2, 0);
		}
		break;

		case 0x81:
		{
			unsigned char outputBuffer[2] = {0x81, 0x00}; // Empty
			if (rs422OutputBuffer.head != rs422OutputBuffer.tail)
			{
				outputBuffer[1] = rs422OutputBuffer.buffer[rs422OutputBuffer.tail];

				if (rs422OutputBuffer.tail + 1 == BUFFER_SIZE)
					rs422OutputBuffer.tail = 0;
				else
					rs422OutputBuffer.tail++;
			}
			writeBytes(outputBuffer, 2, 0);
		}
		break;

		default:
			printf("Error: RS422 Thread %d is an unknown byte\n", buffer[0]);
			arguments->running = 0;
			break;
		}
	}

	printf("RS422 Thread Stopped.\n");
}

int main(int argc, char *argv[])
{
	printf("Card Emulator Version %d.%d\n\n", MAJOR_VERSION, MINOR_VERSION);

	Game game = DERBY_OWNERS_CLUB;

    char *customSerialPath = getenv("CARD_SERIAL_PATH");
    char *serialPath = customSerialPath ? customSerialPath : DEFAULT_SERIAL_PATH;

	int rs422Mode = 1;
	int shutterMode = 1;
	int evenParity = 0;
	int flowControl = 0;
	int baudRate = B2000000;

	printf("      Serial Path: %s\n", serialPath);
	printf("  Connection Mode: %s\n", rs422Mode ? "RS422 Mode" : "RS232 Mode");
	printf("   Emulation Mode: %s\n", shutterMode ? "Shutter" : "No Shutter");
	printf("           Parity: %s\n", evenParity ? "Even" : "None");
	printf("     Flow Control: %s\n\n", flowControl ? "RTS/CTS" : "None");

	if ((serialIO = open(serialPath, O_RDWR | O_NOCTTY | O_SYNC | O_NDELAY)) < 0)
	{
		printf("Error: Could not open %s\n", serialPath);
		return EXIT_FAILURE;
	}

	setSerialAttributes(serialIO, baudRate, evenParity, flowControl);

	pthread_t rs422ThreadID;
	int running = 1;

	if (rs422Mode)
	{
		RS422ThreadArguments arguments = {0};
		arguments.fd = serialIO;
		arguments.running = &running;
		pthread_create(&rs422ThreadID, NULL, rs422Thread, &arguments);
	}

	CardReader reader = {0};
	reader.dispenserFull = 1;
	reader.coverClosed = 0;
	reader.cardPosition = NOT_INSERTED;
	reader.readerStatus = STATUS_NO_ERR;
	reader.jobStatus = STATUS_NO_JOB;

	pthread_t controlThreadID;
	ControlThreadArguments arguments = {0};
	arguments.reader = &reader;
	pthread_create(&controlThreadID, NULL, controlThread, &arguments);

	unsigned char lastCommand = 0x00;

	int inputPacketLength = 0;
	unsigned char inputPacket[BUFFER_SIZE];

	int outputPacketLength = 0;
	unsigned char outputPacket[BUFFER_SIZE];

	int outputPacketDataLength = 0;
	unsigned char outputPacketData[BUFFER_SIZE];

	memset(&tracks, 0x00, 3 * TRACK_SIZE);

	while (running)
	{
		inputPacketLength = readPacket(inputPacket, rs422Mode);

		if (inputPacketLength < 1)
			continue;

		/*printf("ReadPacket: ");
		for (int i = 0; i < inputPacketLength; i++)
		{
			printf("%X ", inputPacket[i]);
		}
		printf("\n");*/

		// Should we send a packet
		if (inputPacketLength == 1 && inputPacket[0] == ENQUIRY)
		{
			// Build the reply packet
			outputPacketLength = 0;
			outputPacket[outputPacketLength++] = lastCommand;

			// Build the status reply bytes
			outputPacket[outputPacketLength++] = getCardStatus(&reader, shutterMode);
			outputPacket[outputPacketLength++] = reader.readerStatus;
			outputPacket[outputPacketLength++] = reader.jobStatus;

			// Copy any data response from the command such as card data
			memcpy(&outputPacket[outputPacketLength], &outputPacketData, outputPacketDataLength);
			outputPacketLength += outputPacketDataLength;
			outputPacketDataLength = 0;

			// Send the packet to the Naomi
			writePacket(outputPacket, outputPacketLength, rs422Mode);

			// Now we run the physical simulation
			if (reader.jobStatus == STATUS_RUNNING_COMMAND)
				reader.jobStatus = STATUS_NO_JOB;

			/*if (reader.cardPosition == NOT_INSERTED)
			{
				reader.cardPosition = INSERTED_IN_FRONT;
				printf("Info: Inserted card\n");
			}*/

			if (reader.cardPosition == EJECTING_CARD)
			{
				reader.cardPosition = NOT_INSERTED;
				printf("Info: Removing card\n");
			}

			continue;
		}

		lastCommand = inputPacket[0];

		switch (inputPacket[0])
		{
		// Initialise the card reader unit
		case INIT:
		{
			printf("Command: Init\n");
			reader.readerStatus = STATUS_NO_ERR;
			reader.jobStatus = STATUS_NO_JOB;
		}
		break;

		// Register a font for printing onto cards
		case REGISTER_FONT:
		{
			printf("Command: Register Font\n");
			reader.readerStatus = STATUS_NO_ERR;
			reader.jobStatus = STATUS_NO_JOB;
		}
		break;

		// Get the status of the card reader unit
		case GET_STATUS:
		{
			printf("Command: Get Status\n");
			reader.readerStatus = STATUS_NO_ERR;
			reader.jobStatus = STATUS_NO_JOB;
		}
		break;

		// Set the shutter on the front of the reader to open/closed
		case SET_SHUTTER:
		{
			reader.coverClosed = (inputPacket[4] == 0x31);
			printf("Command: %s shutter\n", reader.coverClosed ? "Open" : "Closed");
			reader.readerStatus = STATUS_NO_ERR;
			reader.jobStatus = STATUS_NO_JOB;
		}
		break;

		// Clean the magnetic strip on the card to ensure proper electrical contact
		case CLEAN_CARD:
		{
			printf("Command: Clean Card\n");
			reader.coverClosed = 0;
			reader.cardPosition = NOT_INSERTED;
			reader.readerStatus = STATUS_NO_ERR;
			reader.jobStatus = STATUS_NO_JOB;
		}
		break;

		// Physically eject the card from the reader
		case EJECT_CARD:
		{
			printf("Command: Eject Card\n");
			reader.coverClosed = 0;
			reader.cardPosition = EJECTING_CARD;
			reader.readerStatus = STATUS_NO_ERR;
			reader.jobStatus = STATUS_NO_JOB;
		}
		break;

		// Read data from the card
		case READ:
		{
			printf("Command: Read (");

			if (reader.cardPosition == NOT_INSERTED || reader.cardPosition == EJECTING_CARD)
			{
				printf("Error Card not inserted)\n");
				reader.jobStatus = STATUS_WAITING_FOR_CARD;
				break;
			}

			loadCardFromFile();

			char readParam1 = inputPacket[4];
			char readParam2 = inputPacket[5];
			char readParam3 = inputPacket[6];

			if (readParam1 == 0x30)
			{
				int trackIndex[3];
				getTrackIndex(readParam3, trackIndex);

				for (int i = 0; i < 3; i++)
				{
					if (trackIndex[i] == -1)
						continue;
					printf("Track%d, ", i);
					for (int j = 0; j < TRACK_SIZE; j++)
						outputPacketData[outputPacketDataLength++] = tracks[trackIndex[i]][j];
				}
			}
			printf(")\n");

			reader.cardPosition = UNDER_READER;
			reader.readerStatus = STATUS_NO_ERR;
			reader.jobStatus = STATUS_NO_JOB;
		}
		break;

		// Write data to the card
		case WRITE:
		{
			printf("Command: Write (");

			if (reader.cardPosition == NOT_INSERTED || reader.cardPosition == EJECTING_CARD)
			{
				printf("Error Card not inserted)\n");
				reader.jobStatus = STATUS_WAITING_FOR_CARD;
				break;
			}

			char readParam1 = inputPacket[4];
			char readParam2 = inputPacket[5];
			char readParam3 = inputPacket[6];

			if (readParam1 == 0x30)
			{
				int trackIndex[3];
				getTrackIndex(readParam3, trackIndex);

				int inputPacketPointer = 7;
				for (int i = 0; i < 3; i++)
				{
					if (trackIndex[i] == -1)
						continue;
					printf("Track%d, ", i);
					for (int j = 0; j < TRACK_SIZE; j++)
						tracks[trackIndex[i]][j] = inputPacket[inputPacketPointer++];
				}
				printf(")\n");
			}

			reader.cardPosition = UNDER_READER;
			reader.readerStatus = STATUS_NO_ERR;
			reader.jobStatus = STATUS_NO_JOB;

			saveCardToFile();
		}
		break;

		// Erase all of the data on the card
		case ERASE:
		{
			printf("Command: Erase\n");
			for (int i = 0; i < 3; i++)
				for (int j = 0; j < TRACK_SIZE; j++)
					tracks[i][j] = 0x00;
			reader.cardPosition = UNDER_READER;
			reader.readerStatus = STATUS_NO_ERR;
			reader.jobStatus = STATUS_NO_JOB;

			saveCardToFile();
		}
		break;

		// Physically print text/images onto the card
		case PRINT:
		{
			printf("Command: Print\n");
			reader.cardPosition = UNDER_PRINT_HEAD;
			reader.readerStatus = STATUS_NO_ERR;
			reader.jobStatus = STATUS_NO_JOB;
		}
		break;

		// Dispense a new card from the stack of cards in the reader
		case NEW_CARD:
		{
			printf("Command: Get new card\n");
			for (int i = 0; i < 3; i++)
				for (int j = 0; j < TRACK_SIZE; j++)
					tracks[i][j] = 0x00;

			reader.cardPosition = DISPENCING_FROM_BACK;
			reader.coverClosed = 1;
			reader.readerStatus = STATUS_NO_ERR;
			reader.jobStatus = STATUS_NO_JOB;

			saveCardToFile();
		}
		break;

		// Cancel the last operation
		case CANCEL:
		{
			printf("Command: Cancel\n");
			// reader.cardPosition = NOT_INSERTED;
			reader.readerStatus = STATUS_NO_ERR;
			reader.jobStatus = STATUS_NO_JOB;
		}
		break;

		case SET_PRINT_PARAM:
		{
			printf("Command: Set print param\n");
			// reader.cardPosition = UNDER_PRINT_HEAD;
			reader.readerStatus = STATUS_NO_ERR;
			reader.jobStatus = STATUS_NO_JOB;
		}
		break;

		default:
		{
			printf("Error: %X is an unknown command\n", inputPacket[0]);
			running = 0;
			continue;
		}
		}

		// Send the ack reply
		unsigned char ack[] = {ACK};
		int n = writeBytes(ack, 1, rs422Mode);
		/*printf("ACK %d\n", n);*/

		// Seperate for debugging purposes
		// printf("\n");
	}

	if (rs422Mode && rs422ThreadID)
	{
		pthread_join(rs422ThreadID, NULL);
	}

	pthread_join(controlThreadID, NULL);

	closeDevice(serialIO);

	return EXIT_SUCCESS;
}
