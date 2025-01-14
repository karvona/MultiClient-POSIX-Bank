#define _POSIX_C_SOURCE 202009L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include "bank_helper.h"

void test_save_accounts() {
    const char *save_db = "test_saved_accounts.txt";

    struct Account test_accounts[] = {
        {1, 1020.50},
        {2, 250.75},
        {3, 3010.00}
    };
    int test_account_count = sizeof(test_accounts) / sizeof(test_accounts[0]);

    int save_result = save_accounts(test_accounts, test_account_count, save_db);
    assert(save_result == 1);

    printf("Accounts saved.\n", save_db);
}


void test_load_accounts() {
    const char *load_db = "test_saved_accounts.txt";

    struct Account *loaded_accounts = NULL;
    int loaded_count = load_accounts(&loaded_accounts, load_db);
    assert(loaded_count == 3);

    struct Account expected_accounts[] = {
        {1, 1020.50},
        {2, 250.75},
        {3, 3010.00}
    };

    for (int i = 0; i < loaded_count; i++) {
        assert(loaded_accounts[i].id == expected_accounts[i].id);
        assert(loaded_accounts[i].balance == expected_accounts[i].balance);
        pthread_rwlock_destroy(&loaded_accounts[i].lock);
    }

    free(loaded_accounts);

    printf("Accounts loaded");
}

int main() {
    test_save_accounts();
    test_load_accounts();
    printf("All tests passed!\n");
    return 0;
}