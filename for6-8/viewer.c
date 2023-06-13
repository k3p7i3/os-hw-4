#include "common.h"

#define MAX_BUFFER 256

void DieWithError(char *errorMessage) {
    perror(errorMessage);
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {    // Test for correct number of arguments
       fprintf(stderr, "Usage: %s <Server IP> <Server Port>\n", argv[0]);
       exit(1);
    }

    char *serverIP = argv[1];
    unsigned short server_port = atoi(argv[2]);

    int sock;
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
       DieWithError("socket() failed");
    }

    struct sockaddr_in server_address;
    // Construct the server address structure
    memset(&server_address, 0, sizeof(server_address));     // clear structure memory
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);   // server IP address
    server_address.sin_port = htons(server_port);           // server port

    char buffer[MAX_BUFFER];

    int reuse_able = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_able, sizeof(reuse_able));
    if (bind(sock, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
       DieWithError("bind() failed");
    }
    
    while (1) {
        unsigned int from_size = sizeof(server_address);
        int buffer_len;
        struct sockaddr_in response_address;  // address of source client got response from

        if ((buffer_len = recvfrom(sock, buffer, MAX_BUFFER, 0,
                                    (struct sockaddr *) &response_address, &from_size)) < 0) {
            DieWithError("recvfrom() failed");
        }

        buffer[buffer_len] = '\0';
        printf("%s", buffer);    // print null terminated response
    }

    exit(0);
}