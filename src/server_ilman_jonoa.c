#define _POSIX_C_SOURCE 202009L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

#include "bank_helper.h"

#define SOCKET_PATH "/tmp/bank-socket"

#define BUFSIZE 255

#define MAX_ACTIVE_CLIENTS 4  // Amount of counters serving customers

struct Account *accounts = NULL;
int acc_count = 0;
FILE *log_file;
FILE *database = NULL;
pthread_rwlock_t accounts_lock;
pthread_rwlock_t log_lock;
int sock = -1;


void* handle_client(void* arg) {
    int client_socket = (intptr_t)arg;
    char buffer[BUFSIZE];
    ssize_t n;

    send_response(client_socket, "ready\n");

    while ((n = read(client_socket, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0'; 

        char operation;
        int acc_id = -1;
        int dest_id = -1;
        double amount = 0.0;
        char response[BUFSIZE];

        // Parse the operation type
        if (sscanf(buffer, "%c", &operation) < 1) {
            send_response(client_socket, "fail: Invalid command\n");
            continue;
        }

        // Handle different operations
        switch (operation) {
            case 'l': { // Check balance

                if (sscanf(buffer, "%c %d", &operation, &acc_id) != 2 || acc_id < 0) {
                    send_response(client_socket, "fail: Invalid input for balance check\n");
                    break;
                }

                if (create_new_account(&accounts, &acc_count, acc_id, &accounts_lock) != 1) {
                    send_response(client_socket, "fail: Failed to create or find account\n");
                    break;
                }

                struct Account* acc = get_account_by_id(accounts, acc_count, acc_id);

                if (!acc) {
                    send_response(client_socket, "fail: Account not found\n");
                    break;
                }

                double balance = get_balance(acc);
                snprintf(response, sizeof(response), "ok: Account %d balance: %.2f\n", acc_id, balance);
                send_response(client_socket, response);
                break;
            }

            case 'w': { // Withdraw

                if (sscanf(buffer, "%c %d %lf", &operation, &acc_id, &amount) != 3 || acc_id < 0 || amount <= 0) {
                    send_response(client_socket, "fail: Invalid input for withdrawal\n");
                    break;
                }

                if (create_new_account(&accounts, &acc_count, acc_id, &accounts_lock) != 1) {
                    send_response(client_socket, "fail: Failed to create or find account\n");
                    break;
                }

                struct Account* acc = get_account_by_id(accounts, acc_count, acc_id);

                if (!acc || withdraw(acc, amount) != 1) {
                    send_response(client_socket, "fail: Withdraw failed (insufficient funds or invalid account)\n");
                    break;
                }

                snprintf(response, sizeof(response), "ok: Withdraw of %.2f from account %d successful\n", amount, acc_id);
                send_response(client_socket, response);
                if (write_log(log_file, operation, acc_id, -1, amount, &log_lock) == 0) {
                    fprintf(stderr, "Failed to write log after withdraw\n");
                }
                break;
            }

            case 'd': { // Deposit

                if (sscanf(buffer, "%c %d %lf", &operation, &acc_id, &amount) != 3 || acc_id < 0 || amount <= 0) {
                    send_response(client_socket, "fail: Invalid input for deposit\n");
                    break;
                }
                if (create_new_account(&accounts, &acc_count, acc_id, &accounts_lock) != 1) {
                    send_response(client_socket, "fail: Failed to create or find account\n");
                    break;
                }

                struct Account* acc = get_account_by_id(accounts, acc_count, acc_id);

                if (!acc || deposit(acc, amount) != 1) {
                    send_response(client_socket, "fail: Deposit failed (invalid account)\n");
                    break;
                }

                snprintf(response, sizeof(response), "ok: Deposit of %.2f to account %d successful\n", amount, acc_id);
                send_response(client_socket, response);
                if (write_log(log_file, operation, acc_id, -1, amount, &log_lock) == 0) {
                    fprintf(stderr, "Failed to write log after deposit\n");
                }
                break;
            }

            case 't': { // Transfer

                if (sscanf(buffer, "%c %d %d %lf", &operation, &acc_id, &dest_id, &amount) != 4 || acc_id < 0 || dest_id < 0 || amount <= 0) {
                    send_response(client_socket, "fail: Invalid input for transfer\n");
                    break;  
                }

                if (acc_id == dest_id) {
                    send_response(client_socket, "ok: Nothing really happened, but transfer to same account doesn't cause problems\n");
                    break;
                }

                if (create_new_account(&accounts, &acc_count, acc_id, &accounts_lock) != 1) {
                    send_response(client_socket, "fail: Failed to create or find account\n");
                    break;
                }

                if (create_new_account(&accounts, &acc_count, dest_id, &accounts_lock) != 1) {
                    send_response(client_socket, "fail: Failed to create destination account\n");
                    break;
                }

                struct Account* source = get_account_by_id(accounts, acc_count, acc_id);
                struct Account* dest = get_account_by_id(accounts, acc_count, dest_id);

                if (!source || !dest) {
                    send_response(client_socket, "fail: Transfer failed (invalid account)\n");
                    break;
                }

                if (transfer(accounts, acc_count, acc_id, dest_id, amount) != 1) {
                    send_response(client_socket, "fail: Insufficient funds\n");
                    break;
                }

                char response[BUFSIZE];
                snprintf(response, sizeof(response), "ok: Transfer of %.2f from account %d to account %d successful\n", amount, acc_id, dest_id);
                send_response(client_socket, response);
                if (write_log(log_file, operation, acc_id, dest_id, amount, &log_lock) == 0) {
                    fprintf(stderr, "Failed to write log on a transfer\n");
                }
                break;
            }

            case 'q': { // Quit
                send_response(client_socket, "ok: Closing connection...\n");
                close(client_socket);
                return NULL;
            }

            default: {
                send_response(client_socket, "fail: Unknown operation\n");
                break;
            }
        }
    }
    close(client_socket);
    return NULL;
}


// Handle shutdown; Get locks, save account data and close/free everything.
// Getting the locks first makes sure that pending transactions don't fail
// thus no money is lost
void handle_shutdown(int sig) {
    pthread_rwlock_wrlock(&accounts_lock);
    pthread_rwlock_wrlock(&log_lock);

    if (save_accounts(accounts, acc_count, "database.txt") == 0) {
        fprintf(stderr, "Failed to save accounts\n");
    }

    if (sock != -1) {
        close(sock);
        unlink(SOCKET_PATH);
    }
    if (accounts) {
        free(accounts);
    }

    if (log_file) {
        fclose(log_file);
    }
    exit(0);
}


int main(void) {
    database = fopen("database.txt", "w");
    if (load_accounts(&accounts, "database.txt") == -1) {
        fprintf(stderr, "Failed to load accounts from database\n");
        return 0;
    }

    log_file = fopen("log.txt", "a");
    if (log_file == NULL) {
        fprintf(stderr, "Failed to open log file");
        return 0;
    }

    struct sockaddr_un address;
    int sock, conn;
    socklen_t addrLength;

    sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Socket creation failed");
        return 0;
    }

    // Remove any preexisting socket
    unlink(SOCKET_PATH);

    address.sun_family = AF_UNIX;  // Unix domain socket
    strcpy(address.sun_path, SOCKET_PATH);

    addrLength = sizeof(address.sun_family) + strlen(address.sun_path);

    if (bind(sock, (struct sockaddr*)&address, addrLength) < 0) {
        fprintf(stderr, "Bind failed");
        close(sock);
        return 0;
    }

    // Listen for connections
    if (listen(sock, 5) < 0) {
        fprintf(stderr, "Listen failed");
        close(sock);
        return 0;
    }
    printf("Server listening on %s\n", SOCKET_PATH);

    // Handle SIGINT gracefully
    signal(SIGINT, handle_shutdown);

    // Accept clients
    while (1) {
        conn = accept(sock, (struct sockaddr*)&address, &addrLength);
        if (conn < 0) {
            fprintf(stderr, "Accept failed");
            continue;
        }

        printf("Client connected\n");

        // Create a new thread to handle the client
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void*)(intptr_t)conn) != 0) {
            fprintf(stderr, "Thread creation failed");
            close(conn);
            continue;
        }

        pthread_detach(thread_id);
    }

    return 0;
}

int main();