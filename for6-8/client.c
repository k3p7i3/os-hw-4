#include "common.h"

#define TIMEOUT_SECS 5

int sock;
int is_connected = 0;
int event_message_size = sizeof(struct EventMessage);
struct sockaddr_in server_address;  // server address to send messages
int client_id;

int write_time;     // время для написания кода
int review_time;    // время для ревью
int fix_time;       // время для фикса багов

void DieWithError(char *errorMessage) {
    perror(errorMessage);
    exit(1);
}

void closeSources() {
    close(sock);
}

void exitByAlarm(int var) {
    closeSources();
    printf("Couldn't get the answer from server, try launch the client when server is alive\n");
    exit(0);
}

void sendMessage(struct EventMessage *message) {
    int message_size = sendto(sock, message, event_message_size, 0, 
                    (struct sockaddr *) &server_address, sizeof(server_address));
    if (message_size != event_message_size) {
        DieWithError("sendto() sant a different number of bytes");
    }
}

void interruptHandler(int sign) {
    // send message to server about the closure
    struct EventMessage message;
    message.program_id = client_id;
    message.status = EXIT;
    sendMessage(&message);

    closeSources();
    exit(0);
}

int getTime(int avg) {
    // return value in range [avg - 1, avg + 1]
    srand(time(0));
    int diff = rand() % 3 - 1;
    if (avg + diff > 0) {
        return avg + diff;
    }
    return 1;
}

void writeCode() {
    struct EventMessage message;

    // notify server about start
    message.program_id = client_id;
    message.status = IN_PROCESS;
    sendMessage(&message);
    printf("Programmer %d started write code\n", client_id + 1);

    int sleep_time = getTime(write_time);
    sleep(sleep_time);

    message.status = WAIT_FOR_REVIEW;
    sendMessage(&message);
    printf("Programmer %d finished write code\n", client_id + 1);
}

void review(int program_id) {
    struct EventMessage message;

    // notify server about start
    message.program_id = program_id;
    message.status = REVIEW;
    sendMessage(&message);
    printf("Programmer %d started review code for %d\n", client_id + 1, program_id + 1);

    int sleep_time = getTime(review_time);
    sleep(sleep_time);

    // шанс успешного вердикта - 60%, провала - 40%
    srand(time(0));
    int result = rand() % 10;
    if (result < 6) {
        message.status = SUCCESS;
        printf("Programmer %d finished review program %d, status: SUCCESS \n", client_id + 1, program_id + 1);
    } else {
        message.status = FAIL;
        printf("Programmer %d finished review program %d, status: FAIL\n", client_id + 1, program_id + 1);
    }

    sendMessage(&message);
}

void fixCode() {
    struct EventMessage message;

    // notify server about start
    message.program_id = client_id;
    message.status = FIX;
    sendMessage(&message);
    printf("Programmer %d started fix code\n", client_id + 1);

    int sleep_time = getTime(write_time);
    sleep(sleep_time);

    message.status = WAIT_FOR_REVIEW;
    sendMessage(&message);
    printf("Programmer %d finished fix code\n", client_id + 1);
}

void receiveAndHandleServerTask() {
    struct EventMessage message;
    struct sockaddr_in from_address;
    unsigned int address_len = sizeof(from_address);
    int message_size;

    if (!is_connected) {
        alarm(TIMEOUT_SECS); // set timeout for first request
    }
    while ((message_size = recvfrom(sock, &message, event_message_size, 0,
                            (struct sockaddr *)&from_address, &address_len)) < 0) {};

    alarm(0); // cancel the alarm
    is_connected = 1;

    if (message_size != event_message_size) {
        DieWithError("recv() failed");
    }
    if (from_address.sin_addr.s_addr != server_address.sin_addr.s_addr) {
        DieWithError("Received a packet from unknown source.\n");
    }

    if (message.program_id == -1) {     // server stopped working
        closeSources();
        printf("Server stopped working, unable to continue...");
        exit(0);
    }

    // start coding
    if (message.status == IN_PROCESS || message.status == SUCCESS) {
        writeCode();
        return;
    }

    // review another client's code
    if (message.status == WAIT_FOR_REVIEW) {
        review(message.program_id);
        return;
    }

    if (message.status == FAIL) {
        fixCode();
        return;
    }
}

int main(int argc, char *argv[]) {
    (void)signal(SIGINT, interruptHandler);
    (void)signal(SIGALRM, exitByAlarm);

    unsigned short server_port;       
    char *serverIP;                   

    if (argc != 7) {    // Test for correct number of arguments
       fprintf(stderr, "Usage: %s <ClientID> <Server IP> <Server Port> <Writing code time> <Review time> <Fix time>\n",
               argv[0]);
       exit(1);
    }

    client_id = atoi(argv[1]) - 1;
    serverIP = argv[2];
    server_port = atoi(argv[3]);
    write_time = atoi(argv[4]);
    review_time = atoi(argv[5]);
    fix_time = atoi(argv[6]);

    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        DieWithError("socket() failed");
    }
        
    // Construct the server address structure
    memset(&server_address, 0, sizeof(server_address));     // clear structure memory
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = inet_addr(serverIP);   // server IP address
    server_address.sin_port = htons(server_port);           // server port

    // send first message - notify server about new client
    struct EventMessage first_message;
    first_message.program_id = client_id;
    first_message.status = IN_PROCESS;
    sendMessage(&first_message);

    for (;;) {
        receiveAndHandleServerTask();
    }

    closeSources();
    exit(0);
}