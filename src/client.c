#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

#define BUFSIZE 255
#define SOCKET_PATH "/tmp/bank-socket"
int sock;

// Helper function to send message to server using socket
void send_to_server(int sock, const char *message) {
    ssize_t n = write(sock, message, strlen(message));
    if (n < 0) {
        fprintf(stderr, "Failed to send message to server\n");
        exit(1);
    }
}

// Helper function to receive messages from server from socket.
void receive_from_server(int sock) {
    char buffer[BUFSIZE];
    ssize_t n = read(sock, buffer, sizeof(buffer) - 1);
    if (n < 0) {
        fprintf(stderr, "Failed to receive message from server\n");
        exit(1);
    }
    buffer[n] = '\0';
    printf("%s", buffer);
}

// Handle sigint sent from terminal to announce server that client disconnected.
void handle_sigint(int sig) {
    if (sig == SIGINT) {
        if (sock != -1) {
            send_to_server(sock, "q\n");
            receive_from_server(sock);
            close(sock);
        }
    }
}
 

// Main client code
int main(int argc, char **argv) {
    setvbuf(stdin, NULL, _IOLBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("Connecting to the bank, please wait.\n");

    // Init unix domain socket connection to server
    sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Failed to create socket\n");
        return -1;
    }

    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(struct sockaddr_un));
    server_addr.sun_family = AF_UNIX;
    strcpy(server_addr.sun_path, SOCKET_PATH);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_un)) < 0) {
        fprintf(stderr, "Failed to connect to server");
        close(sock);
        return -1;
    }   

    // Assign signal handler to handle CTRL+C SIGINT from terminal
    signal(SIGINT, handle_sigint);

    int quit = 0;
    char *buf = malloc(BUFSIZE);
    if (buf == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        close(sock);
        return -1;
    }

    printf("Connected to server\n");

    // Wait for "ready"-message from server
    receive_from_server(sock);

    // Loop to ask client for commands until getting 'q' or SIGINT
    while (!quit) {
        printf("Enter command:\n");
        if (fgets(buf, BUFSIZE, stdin) == NULL) {
            break;
        }

        // Handle commands; send the command to server and receive answer from server.
        switch (buf[0]) {
            case 'q':  // Quit
                quit = 1;
                // Notify server and receive closing message
                send_to_server(sock, "q\n");
                receive_from_server(sock);
                break;

            case 'l':  // Get balance
                send_to_server(sock, buf);
                receive_from_server(sock);
                break;

            case 'w':  // Withdraw
                send_to_server(sock, buf);
                receive_from_server(sock);
                break;

            case 'd':  // Deposit
                send_to_server(sock, buf);
                receive_from_server(sock);
                break;

            case 't':  // Transfer
                send_to_server(sock, buf);
                receive_from_server(sock);
                break;

            default:  // Unknown command
                printf("fail: Unknown command\n");
                break;
        }
    }

    // Clean up and close the socket
    free(buf);
    close(sock);
    return 0;
}