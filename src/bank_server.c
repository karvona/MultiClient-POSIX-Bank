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
#include <sys/msg.h>
#include <errno.h>

#include "bank_helper.h"

// Socket and database file paths.
#define SOCKET_PATH "/tmp/bank-socket"
#define DATABASE_FILE "database.txt"

#define BUFSIZE 255

// Message queue
#define MAX_ACTIVE_CLIENTS 4  // Amount of counters serving customers
#define MESSAGE_QUEUE_KEY 1234
#define MAX_CLIENTS 100 // Max amount of client sockets in queue

// Init accounts, log, database and rwlocks for accounts
struct Account *accounts = NULL;
int acc_count = 0;
FILE *log_file;
FILE *database = NULL;
pthread_rwlock_t accounts_lock;
pthread_rwlock_t log_lock;
int sock = -1;

// Init message queue arrays and their lenghts & mutex for editing them
int message_queue;
int message_queues[MAX_ACTIVE_CLIENTS];
int queue_lengths[MAX_ACTIVE_CLIENTS] = {0};
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;


// Loop to handle a client once they reach the desk. 
// Called by the service desk.
void* handle_client(void* arg) {
    int client_socket = (intptr_t)arg;
    char buffer[BUFSIZE];
    ssize_t n;

    // Once code reaches here, customer has reached the desk from the queue,
    // meaning the client is now served and can thus be notified with "ready".
    send_response(client_socket, "ready\n");

    // Read clients messages from the socket
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

        // Handle different operations requested by the customer.
        // Mostly just error checking and calling the helper funtions.
        // If operation succeeds, notify client with message beginning with "ok: ...",
        // in case of failure, "fail: ..." instead.
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

            case 'w': { // Withdraw money from chosen account

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

            case 'd': { // Deposit money to chosen account

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

            case 't': { // Transfer money between source and destination account

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
                printf("Client disconnected\n");
                break;
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


// Code of a single service desk thread, initialized in the main loop.
// Accepts customers from the queue with same id and handles them by calling handle_client.
void* service_desk(void* arg) {
    int desk_id = *(int*)arg;
    free(arg);
    printf("Desk %d is waiting for client.\n", desk_id);

    while (1) {
        struct Client_message msg;

        // Take queue mutex to ensure correct behaviour when operating on queues
        pthread_mutex_lock(&queue_mutex);

        // If client in queue, try to serve it
        if (queue_lengths[desk_id] >= 0) {
            if (msgrcv(message_queues[desk_id], &msg, sizeof(msg), 0, IPC_NOWAIT) != -1) {
                printf("Desk %d received client %d from the queue.\n", desk_id, msg.client_socket);
            } 
            else {
                pthread_mutex_unlock(&queue_mutex);
                continue;
            }
        } 
        else {
            pthread_mutex_unlock(&queue_mutex);
            continue;
        }

        pthread_mutex_unlock(&queue_mutex);

        int client_socket = msg.client_socket;

        handle_client((void*)(intptr_t)client_socket);

        // Lock the queue mutex to decrease the queue size after handling the customer.
        pthread_mutex_lock(&queue_mutex);
        queue_lengths[desk_id]--;
        pthread_mutex_unlock(&queue_mutex);
        close(client_socket);
    }
}


// Handle shutdown; Get locks, save account data and close/free everything.
// Getting the locks first makes sure that pending transactions don't fail
// thus no money is lost
void handle_shutdown(int sig) {
    pthread_rwlock_wrlock(&accounts_lock);
    pthread_rwlock_wrlock(&log_lock);

    if (save_accounts(accounts, acc_count, DATABASE_FILE) == 0) {
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
    // Open database in append mode to create it if it doesn't exist, but if it does, not overwrite anything.
    FILE *file = fopen(DATABASE_FILE, "a+");
    if (file == NULL){
        fprintf(stderr, "Failed to open database file");
    }
    fclose(file);

    // Load accounts from database to memory.
    acc_count = load_accounts(&accounts, DATABASE_FILE);

    if ( acc_count == -1) {
        fprintf(stderr, "Failed to load accounts from database\n");
        return 0;
    }
    if (acc_count == 0) {
        printf("Database is initially empty. No accounts to load.\n");
    }
    printf("\n");

    // Open log file to write account operations to log
    log_file = fopen("log.txt", "a");
    if (log_file == NULL) {
        fprintf(stderr, "Failed to open log file\n");
        return 0;
    }

    // Init message queues for service desks to queue clients.
    for (int i = 0; i < MAX_ACTIVE_CLIENTS; i++) {
        message_queues[i] = msgget(MESSAGE_QUEUE_KEY + i, IPC_CREAT | 0644);

        if (message_queues[i] == -1) {
            fprintf(stderr, "Failed to open message queue\n");
            return 0;
        }
        printf("Queue for service desk %d initialized.\n", i);
    }

    struct sockaddr_un address;
    int sock, conn;
    socklen_t addrLength;

    // Init unix domain socket for ipc between server and clients
    sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Socket creation failed\n");
        return 0;
    }

    // Remove any preexisting socket
    unlink(SOCKET_PATH);

    address.sun_family = AF_UNIX;  // Unix domain socket
    strcpy(address.sun_path, SOCKET_PATH);

    addrLength = sizeof(address.sun_family) + strlen(address.sun_path);

    // Bind socket to socket name
    if (bind(sock, (struct sockaddr*)&address, addrLength) < 0) {
        fprintf(stderr, "Bind failed\n");
        close(sock);
        return 0;
    }

    // Listen for connections
    if (listen(sock, 5) < 0) {
        fprintf(stderr, "Listen failed\n");
        close(sock);
        return 0;
    }

    printf("\nServer listening on %s\n\n", SOCKET_PATH);

    // Handle SIGINT and SIGTERM gracefully by calling the handle_shutdown-function
    signal(SIGINT, handle_shutdown);
    signal(SIGTERM, handle_shutdown);

    // Initialize the service desks (threads)
    for (int i = 0; i < MAX_ACTIVE_CLIENTS; i++) {
        int* desk_id = malloc(sizeof(int));
        *desk_id = i;
        pthread_t desk_thread;
        pthread_create(&desk_thread, NULL, service_desk, desk_id);
        pthread_detach(desk_thread);
    }


    while (1) {
        // Accept new clients and send them so shortest queue
        conn = accept(sock, (struct sockaddr*)&address, &addrLength);
        if (conn > 0) {
            printf("Client connected\n");

            // Lock the queue mutex and find shortest queue.
            pthread_mutex_lock(&queue_mutex);
            int shortest_q = shortest_queue(queue_lengths, MAX_ACTIVE_CLIENTS);
            pthread_mutex_unlock(&queue_mutex);

            struct Client_message msg;
            msg.mtype = 1;
            msg.client_socket = conn;

            printf("Assigning client %d to queue %d\n", conn, shortest_q);

            // Send the client socket to the shortest queue
            if (msgsnd(message_queues[shortest_q], &msg, sizeof(msg), 0) == -1) {
                fprintf(stderr, "Failed to add client to queue %d\n", shortest_q);
                close(conn);
            } else {
                // Lock the queue mutex to increase the queue size.
                pthread_mutex_lock(&queue_mutex);
                queue_lengths[shortest_q]++;
                pthread_mutex_unlock(&queue_mutex);
            }   
        }
    }

    // This part of the code should never be reached, but just in case handle the shutdown correctly.
    handle_shutdown(SIGINT); 
    return 0; 
}