# ThreadBank - Simple Bank Server with Multiple Service Desks

## Overview

ThreadBank is a server application simulating a basic banking system. The bank server allows multiple clients to perform various banking operations (such as depositing, withdrawing, and transferring money) concurrently using threads and inter-process communication (IPC). The server is designed to handle multiple clients, each connecting to a service desk managed by its own thread. Clients are placed into the shortest queue for their respective desk, and communication with the desk happens over Unix domain sockets.

Account data is stored in a persistent file, and the system employs synchronization mechanisms like read-write locks to ensure data consistency across multiple threads.


### Features:
+ Multiple service desks, each running in a separate thread
+ Each desk has its own queue for managing client requests
+ IPC communication using Unix domain sockets between the client and server
+ Persistent account data stored in a text file with read-write locking for thread safety
+ Graceful server shutdown upon receiving SIGINT/SIGTERM without data corruption

### Prerequisites
+ A Unix-like system (Linux, macOS)
+ GCC or any compatible C compiler
+ Pthreads library
+ POSIX or SysV message queues (configured on the system)


## To build and run the code on a Linux system
First, clone the repository to your local machine:

`$ git clone https://github.com/karvona/MultiClient-POSIX-Bank.git`

Go to the repository folder

`$ cd <repository_folder>`

Create build folder

`$ mkdir build`

Build the project using the Makefile

`$ make`

This will generate the necessary executables (server and client).

### Running the server
   
To start the server, simply run:

`$ ./build/bank_server`

The server will initialize and begin listening for client connections on a Unix domain socket.

### Running the client
   
To run the client (in different terminal), use:

`$ ./build/client`

Once connected, the client can interact with the server by issuing commands such as:
+ l <account_id>: Check balance of a specified account
+ w <account_id> <amount>: Withdraw a specified amount from an account
+ t <source_account_id> <target_account_id> <amount>: Transfer money between accounts
+ d <account_id> <amount>: Deposit money into an account
+ q: Quit the client session

### Shutdown
To gracefully shut down the server, send a SIGINT (Ctrl+C) or SIGTERM signal to the server process. The server will close the connections and persist any outstanding account data.


### Project Summary
The ThreadBank project implements a simple multithreaded server simulating a bank's operations. The server manages multiple service desks (threads), each serving a client, with clients entering the shortest queue. The system ensures thread safety by using read-write locks to prevent data corruption. Client communication is handled via Unix domain sockets, and account information is stored persistently in a text file. The system gracefully shuts down on receiving termination signals, ensuring no loss of data.


### Possible Improvements:
+ Implement timeouts for inactive clients at the service desks.
+ Introduce thread sleep time for idle threads to reduce CPU usage.
