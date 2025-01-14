#define _POSIX_C_SOURCE 202009L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

struct Account {
    int id;
    double balance;
    pthread_rwlock_t lock;
};

struct Client_message {
    long mtype;
    int client_socket;
};

int save_accounts(struct Account *accounts, int acc_count, const char *database);

int load_accounts(struct Account **accounts, const char *database);

int create_new_account(struct Account **accounts, int *acc_count, int account_id, 
                        pthread_rwlock_t *accounts_lock);

struct Account* get_account_by_id(struct Account* accounts, int acc_count, int id);

int write_log(FILE *log_file, char operation, int account_id, int dest_id, 
                double amount, pthread_rwlock_t *log_lock);

double get_balance(struct Account *account);

int deposit(struct Account *account, double amount);

int withdraw(struct Account *account, double amount);

int transfer(struct Account *accounts, int acc_count, int source_id, int dest_id,
                double amount);

void send_response(int client_socket, const char *response);

int shortest_queue(const int *queue_lengths, int num_queues);