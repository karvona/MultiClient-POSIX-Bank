#define _POSIX_C_SOURCE 202009L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "bank_helper.h" 

// Some printed error messages are commented out so the server terminal is easier to read.
// Currently, server terminal only prints necessary messages.


// Save account from memory do database text file.
// First row only has account count, other rows contain data of each account.
int save_accounts(struct Account *accounts, int acc_count, const char *database) {
    FILE *file = fopen(database, "w");
    if (!file) {
        fprintf(stderr, "Failed to open database file to save accounts: %s\n", database);
        return 0;
    }

    // Write account count to the first line of database file
    fprintf(file, "%d\n", acc_count);

    // Write account data to one row per customer
    // Format : <account id>, <balance> 
    for (int i = 0; i < acc_count; i++) {
        if (fprintf(file, "%d %.2f\n", accounts[i].id, accounts[i].balance) < 0) {
            fprintf(stderr, "Failed to write account %d\n", i);
            fclose(file);
            return 0;
        }
        printf("Saved Account %d: ID = %d, Balance = %.2f\n", i + 1, accounts[i].id, accounts[i].balance);
    }

    fclose(file);
    return 1;
}


// Load accounts from the database text file to server memory.
int load_accounts(struct Account **accounts, const char *database) {
    FILE *file = fopen(database, "r");
    if (!file) {
        fprintf(stderr, "Failed to open database file: %s\n", database);
        return -1;
    }

    int acc_count = 0;  // Read account count from the first line

    // Read account count from first row. If file is empty, will be 0.
    int read_count = fscanf(file, "%d\n", &acc_count);
    if (read_count == 0){
        printf("Database file initially empty.");
    }

    if ( acc_count < 0) {
        fprintf(stderr, "Failed to load account count");
        fclose(file);
        *accounts = NULL;
        return -1;
    }

    *accounts = (struct Account *)malloc(acc_count * sizeof(struct Account));
    if (!*accounts) {
        fprintf(stderr, "Failed to allocate memory for accounts\n");
        fclose(file);
        return -1;
    }

    // Read each row with account data to load the account to the accounts array.
    for (int i = 0; i < acc_count; i++) {
        if (fscanf(file, "%d %lf\n", &(*accounts)[i].id, &(*accounts)[i].balance) != 2) {
            fprintf(stderr, "Failed to read account %d\n", i);
            free(*accounts);
            fclose(file);
            return 0;
        }
        printf("Loaded Account %d: ID = %d, Balance = %.2f\n", i + 1, (*accounts)[i].id, (*accounts)[i].balance);
        // Initialize rw locks for each account
        pthread_rwlock_init(&(*accounts)[i].lock, NULL);
    }
    fclose(file);
    return acc_count;
}


// Create new account to the memory with 0 balance.
// This is mostly used to handle client requests concerning accounts that do not already exist in the memory,
// in which case the account is created with 0 balance. The bank could also have been implemented in a
// way that accounts need to be created by for example depositing first, but this way seemed handier,
// since the bank still functions correctly with empty accounts.

int create_new_account(struct Account **accounts, int *acc_count, int account_id, pthread_rwlock_t *accounts_lock) {
    // Lock the whole accounts list to make sure two accounts aren't created on top of each other in parallel
    pthread_rwlock_wrlock(accounts_lock);

    // Check if the account already exists
    for (int i = 0; i < *acc_count; i++) {
        if ((*accounts)[i].id == account_id) {
            pthread_rwlock_unlock(accounts_lock);
            return 1;
        }
    }

    // If the account doesn't exist, create a new account
    *accounts = realloc(*accounts, (*acc_count + 1) * sizeof(struct Account));
    if (*accounts == NULL) {
        fprintf(stderr, "Memory allocation for new account failed\n");
        pthread_rwlock_unlock(accounts_lock);
        return 0;
    }
    // Initialize the new account struct
    (*accounts)[*acc_count].id = account_id;
    (*accounts)[*acc_count].balance = 0.0;
    pthread_rwlock_init(&(*accounts)[*acc_count].lock, NULL);

    // Update the account count
    (*acc_count)++;

    // Unlock the accounts list after reallocating and adding the new account
    pthread_rwlock_unlock(accounts_lock);
    return 1;
}


// Get an account given its ID
struct Account* get_account_by_id(struct Account* accounts, int acc_count, int id) {
    for (int i = 0; i < acc_count; i++) {
        if (accounts[i].id == id) {
            return &accounts[i];
        }
    }
    return NULL;
}


// Write operations to log file
int write_log(FILE *log_file, char operation, int account_id, int dest_id, double amount, pthread_rwlock_t *log_lock) {
    if (log_file == NULL) {
        fprintf(stderr, "Log file isn't open\n");
        return 0;
    }

    pthread_rwlock_wrlock(log_lock);

    if (dest_id == -1) {
        if (fprintf(log_file, "%c: Account %d, Amount %.2f\n", operation, account_id, amount) < 0) {
            fprintf(stderr, "Failed to write to log file");
            pthread_rwlock_unlock(log_lock);
            return 0;
        }
    }
    else {
        if (fprintf(log_file, "%c: Account %d, Destination %d, Amount %.2f\n", operation, account_id, dest_id, amount) < 0) {
            fprintf(stderr, "Failed to write to log file");
            pthread_rwlock_unlock(log_lock);
            return 0;
        }
    }
    pthread_rwlock_unlock(log_lock);
    return 1;
}


// Get balance of given account
double get_balance(struct Account *account) {
    double balance;

    // Get write lock for given account, only then get the balance
    pthread_rwlock_rdlock(&account->lock);
    balance = account->balance;
    pthread_rwlock_unlock(&account->lock);

    return balance;
}


// Deposit amount to given account
int deposit(struct Account *account, double amount) {
    if (amount <= 0) {
        fprintf(stderr, "Deposit amount should be positive\n");
        return 0;
    }
    // Get the lock for the account, only then increase the balance.
    pthread_rwlock_wrlock(&account->lock);
    account->balance += amount;
    pthread_rwlock_unlock(&account->lock);

    return 1;
}


// Withdraw amount from given account
int withdraw(struct Account *account, double amount) {
    if (amount <= 0) {
        fprintf(stderr, "Withdraw amount should be positive\n");
        return 0;
    }

    // Get the lock for the account, or block until you get it
    pthread_rwlock_wrlock(&account->lock);

    // Check the account balance AFTER getting the lock
    if (amount > account->balance) {
        //fprintf(stderr, "Account has insufficient funds\n");
        pthread_rwlock_unlock(&account->lock);
        return 0;
    }
    // Decrease the amount and unlock the write lock
    account->balance -= amount;
    pthread_rwlock_unlock(&account->lock);

    return 1;
}


// Transfer money between source and destination account
int transfer(struct Account *accounts, int acc_count, int source_id, int dest_id, double amount) {

    // If source same as destination, let server take action and just return with 1 early
    // This case should never be reached due to server code handling it.
    if (source_id == dest_id) {
        return 1;
    }

    // Find the source and destination accounts
    struct Account *source_account = get_account_by_id(accounts, acc_count, source_id);
    struct Account *dest_account = get_account_by_id(accounts, acc_count, dest_id);

    if (source_account == NULL) {
        //fprintf(stderr, "Source account not found.\n");
        return 0;
    }
    
    if (dest_account == NULL) {
        //fprintf(stderr, "Destination account not found.\n");
        return 0;
    }

    // Lock the accounts smaller id first, to prevent deadlock in situations where same 
    // accounts transfer money to both directions concurrently.
    // This way the locks are always taken in same order, preventing deadlock possibility.
    if (source_id < dest_id) {
        pthread_rwlock_wrlock(&source_account->lock);
        pthread_rwlock_wrlock(&dest_account->lock);
    } else {
        pthread_rwlock_wrlock(&dest_account->lock);
        pthread_rwlock_wrlock(&source_account->lock);
    }

    if (source_account->balance < amount) {
        //fprintf(stderr, "Insufficient funds in source account.\n");
        pthread_rwlock_unlock(&source_account->lock);
        pthread_rwlock_unlock(&dest_account->lock);
        return 0;
    }

    // Move the money, or actually just decrease it from source and add to destination.
    source_account->balance -= amount;
    dest_account->balance += amount;

    pthread_rwlock_unlock(&source_account->lock);
    pthread_rwlock_unlock(&dest_account->lock);

    return 1;
}


// Send responses to the client trough the  socket.
void send_response(int client_socket, const char *response) {
    write(client_socket, response, strlen(response));
}


// Return shortest queue to assign new client to
int shortest_queue(const int *queue_lengths, int num_queues) {
    int shortest_index = 0;
    int min_length = queue_lengths[0];

    for (int i = 1; i < num_queues; i++) {
        if (queue_lengths[i] < min_length) {
            min_length = queue_lengths[i];
            shortest_index = i;
        }
    }
    return shortest_index;
}