#include "common.h"
#include <pthread.h>

#define CLIENTS_NUMBER 3
#define MAX_BUFFER 256

int server_socket;

int broadcast_socket;
struct sockaddr_in broadcast_address;
char notify_buffer[MAX_BUFFER];

struct Program programs[CLIENTS_NUMBER];                    // server stores information about all 3 programs
struct sockaddr_in clients_addr[CLIENTS_NUMBER];     // server stores programmer clients sockets
int clients_active[CLIENTS_NUMBER];
int client_balance[CLIENTS_NUMBER];                 // the number of tasks the client is able to do right now (0 or 1)

void DieWithError(char *errorMessage) {
    perror(errorMessage);
    exit(1);
}

int createServerSocket(unsigned short port) {
    int sock;
    struct sockaddr_in server_address;

    // create socket
    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        DieWithError("socket() failed");
    }

    // construct address
    memset(&server_address, 0, sizeof(server_address)); // clear memory for address
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // make any interface available
    server_address.sin_port = htons(port);

    // try to bind socket with constructed address
    if (bind(sock, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
        DieWithError("bind() failed");
    }

    return sock;
}

int createBroadcastSocket(int port, char* ip) {
    int sock;

    // create socket
    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        DieWithError("socket() failed");
    }

    int broadcastPermission = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (void *) &broadcastPermission, 
                    sizeof(broadcastPermission)) < 0) {
        DieWithError("setsockopt() failed");
    }
       
    // Construct local address structure
    memset(&broadcast_address, 0, sizeof(broadcast_address)); 
    broadcast_address.sin_family = AF_INET;                 
    broadcast_address.sin_addr.s_addr = inet_addr(ip);
    broadcast_address.sin_port = htons(port);

    return sock;
}

struct EventMessage createMessage(int program_id, enum ProgramStatus status) {
    // build new message with given parameters
    struct EventMessage message;
    message.program_id = program_id;
    message.status = status;
    return message;
}

void sendMessage(int client_id, struct EventMessage *message) {
    if (clients_active[client_id]) {    // check that client is active
        // send message to client
        int sent_len = sendto(server_socket, message, sizeof(struct EventMessage), 0,
                            (struct sockaddr *)&clients_addr[client_id], sizeof(clients_addr[client_id]));
        if (sent_len != sizeof(struct EventMessage)) {
            DieWithError("sendto() sent a different number of bytes");
        }

        --client_balance[client_id];  // server sent task -> update balance
    }
}

void createNotifyString(int client_id, struct EventMessage *message) {
    // creates from message structure string explanation for vivistor client
    if (message->status == IN_PROCESS) {
        sprintf(notify_buffer, "Programmer %d started write code...\n", client_id + 1);
    }
    if (message->status == WAIT_FOR_REVIEW) {
        sprintf(notify_buffer, "Programmer %d sent his task for review to programmer %d\n", client_id + 1, programs[client_id].reviewer_id + 1);
    }
    if (message->status == REVIEW) {
        sprintf(notify_buffer, "Programmer %d started review program %d\n", client_id + 1, message->program_id + 1);
    }
    if (message->status == FAIL) {
        sprintf(notify_buffer, "Programmer %d finished review of task %d with status: FAIL\n", client_id + 1, message->program_id + 1);
    }
    if (message->status == SUCCESS) {
        sprintf(notify_buffer, "Programmer %d finished review of task %d with status: SUCCESS\n", client_id + 1, message->program_id + 1);
    }
    if (message->status == FIX) {
        sprintf(notify_buffer, "Programmer %d started fix code...\n", client_id + 1);
    }
    if (message->status == EXIT) {
        sprintf(notify_buffer, "Client %d stopped working.\n", client_id + 1);
    }
}

void sendMessageToViewers(int client_id, struct EventMessage *message) {
    createNotifyString(client_id, message);
    unsigned int buffer_len = strlen(notify_buffer);
    if (sendto(broadcast_socket, notify_buffer, buffer_len, 0, 
                (struct sockaddr *)&broadcast_address, sizeof(broadcast_address)) != buffer_len) {
        DieWithError("sendto sent different number of bytes");
    }
}

struct EventMessage receiveMessage() {
    // receive message from client
    struct EventMessage message;
    struct sockaddr_in client_address;
    unsigned int client_addr_len = sizeof(client_address);

    int message_size = recvfrom(server_socket, &message, sizeof(message), 0,
                                (struct sockaddr *)&client_address, &client_addr_len);
    if (message_size < 0) {
        DieWithError("recvrom failed");
    }

    int program_id = message.program_id;

    if (program_id > 2) {
        DieWithError("Client id is too high");
    }
    if (message.status == IN_PROCESS && !clients_active[program_id]) {
        // server got the first message from this client
        clients_addr[program_id] = client_address;
        clients_active[program_id] = 1;
        client_balance[program_id] = 1;
        sendMessage(program_id, &message); // send the message as a command
    } else {
        sendMessageToViewers(program_id, &message);
    }

    // клиент оповещает сервер о своем закрытии
    if (message.status == EXIT) {
        clients_active[program_id] = 0; // сделать клиент не активным
    }
    return message;
}

int getReviewerId(int author_id) {
    srand(time(0));
    // выбрать случайного программиста, который будет проверять задачу
    int reviewer_id;
    do {
        reviewer_id = rand() % CLIENTS_NUMBER;
    } while (reviewer_id == author_id);

    return reviewer_id;
}

void handleClientMessage(struct EventMessage *message) {
    // обработка события, о котором сообщил клиент
    int program_id = message->program_id;
    programs[program_id].status = message->status;

    if (message->status == WAIT_FOR_REVIEW) {
        if (programs[program_id].reviewer_id == -1) {
            // назначить ревьюера для задачи, если его еще нет
            programs[program_id].reviewer_id = getReviewerId(program_id);
        }
        ++client_balance[program_id]; // programmer now is free -> uodate balance
    }

    if (message->status == SUCCESS || message->status == FAIL) {
        // reviewer now is free -> udpdate balance
        ++client_balance[programs[program_id].reviewer_id];
    }

    if (message->status == IN_PROCESS) {
        // если клиент начал писать новую программу, то обнулить предыдущего ревьюера
        programs[program_id].reviewer_id = -1;
    }
}

void closeSources() {
    // завершение работы сервера
    close(server_socket);
}

void interruptHandler(int sign) {
    struct EventMessage final = createMessage(-1, EXIT);
    for (int i = 0; i < CLIENTS_NUMBER; ++i) {
        if (clients_active[i]) {
            sendMessage(i, &final);
        }
    }

    sendMessageToViewers(-1, &final);
    closeSources();
    exit(0);
}

void findSendTasks() {
    struct EventMessage message;
    // enumerate all clients
    for (int client = 0; client < CLIENTS_NUMBER; ++client) {
        // if client inactive or client is busy (balance is not positive) - then skip
        if (!clients_active[client] || client_balance[client] <= 0) {
            continue;
        }

        for (int program = 0; program < CLIENTS_NUMBER; ++program) {
            if (program == client) {
                if (programs[program].status == FAIL) {
                    // взять задачу внесения правок в свой код
                    message.program_id = client;
                    message.status = FAIL;
                    sendMessage(client, &message);
                    return;
                }
            } else {
                // program != client
                if (programs[program].reviewer_id == client && programs[program].status == WAIT_FOR_REVIEW) {
                    message.program_id = program;
                    message.status = WAIT_FOR_REVIEW;
                    sendMessage(client, &message);
                    return;
                }
            }
        }

        // write iwn code only when there's no other tasks
        if (programs[client].status == SUCCESS) {
            message.program_id = client;
            message.status = SUCCESS;
            sendMessage(client, &message);
            return;
        }
    }
}

int main(int argc, char *argv[]) {
    (void)signal(SIGINT, interruptHandler);

    unsigned short server_port;
    unsigned short broadcast_port;
    char *broadcast_ip;
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <SERVER PORT> <BROADCAST PORT> <BROADCAST IP>\n", argv[0]);
        exit(1);
    }

    server_port = atoi(argv[1]);
    server_socket = createServerSocket(server_port);

    broadcast_port = atoi(argv[2]);
    broadcast_ip = argv[3];
    broadcast_socket = createBroadcastSocket(broadcast_port, broadcast_ip);

    while (1) {
        struct EventMessage message = receiveMessage();
        handleClientMessage(&message);
        findSendTasks();
    };

    closeSources();
    return 0;
}