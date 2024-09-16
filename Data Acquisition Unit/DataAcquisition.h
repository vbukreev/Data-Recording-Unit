#ifndef _DATAACQUISITION_H
#define _DATAACQUISITION_H
#include <errno.h>
#include <iostream>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <queue>
#include <netinet/in.h>
#include "SeismicData.h"
#include <map>
#include <list>
#include <algorithm>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

// Signal handler for SIGINT
static void setupSigintHandler(int signum);

class DataAcquisition {
public:
    static DataAcquisition* instance;

    // Constructor and Destructor
    DataAcquisition();
    ~DataAcquisition();

    // Core functionality methods
    void receiveFunction(); // Handles incoming data
    void WriteFunction();   // Sends data to subscribers
    void executeData();     // Main execution loop
    void shutdown();        // Shuts down the data acquisition system
    void processDataLoop();
    void shutdownDataAcquisition();
    


    // Shared memory and synchronization
    key_t ShmKey;
    int ShmID;
    struct SeismicMemory *ShmPTR;
    sem_t *sem_id1;

    // Thread management
    pthread_t read_thread, write_thread;
    pthread_mutex_t lock_x;

    // Network communication
    int sockfd;
    struct sockaddr_in server_addr;
    bool is_running = false;
    struct sigaction action;

    // Data structures for packets and subscribers
    struct DataPacket {
        unsigned short packet_length;
        uint16_t packet_number;
        SeismicData data;
    };

    struct Subscriber {
        string username;
        struct sockaddr_in address;
        uint16_t port;
        
         Subscriber(const std::string& user, const sockaddr_in& addr, uint16_t p)
        : username(user), address(addr), port(p) {}
    };



    //void processDataLoop();
    //void shutdownDataAcquisition();
    DataPacket createDataPacket(int index);
    void addToDataQueue(const DataPacket& packet);
    int updateIndex(int currentIndex);
    
    
    // Initialization methods
    void initializeSystemComponents(); // Initializes system components
    void startDataThreads();           // Starts the data processing threads
    void initializeUdp();              // Initializes UDP socket
    void semInitialize();              // Initializes semaphore
    int SharedMemory();                // Initializes shared memory
    void interruptInitializer();       // Sets up interrupt handler
    void threadInitializer();          // Initializes threads
    void socketInitializer();          // Initializes socket

    // Subscriber management
    void SubscriberAdd(const std::string& username, const sockaddr_in& client_addr); // Adds a new subscriber
    void removeSubscriber(const sockaddr_in& client_addr);                           // Removes a subscriber
    void updateRogue(const std::string& username, const sockaddr_in& client_addr);  // Updates rogue centers

    // Helper functions for receiveFunction
    void processReceivedData(char* buffer, const sockaddr_in& client_addr, std::map<std::string, int>& ip_connection_count);
    bool isRogueClient(const sockaddr_in& client_addr);
    std::string getClientIpAndPort(const sockaddr_in& client_addr);
    void parseBuffer(char* buffer, char* actionString, char* username, char* password);
    void handleSubscription(const std::string& username, const sockaddr_in& client_addr, const std::string& ip_and_port, std::map<std::string, int>& ip_connection_count);
    void handleCancellation(const std::string& username, const sockaddr_in& client_addr, const std::string& ip_and_port, std::map<std::string, int>& ip_connection_count);
    void checkForRogueActivity(const std::string& username, const sockaddr_in& client_addr, const std::string& ip_and_port, std::map<std::string, int>& ip_connection_count);
    bool isAlreadySubscribed(const sockaddr_in& client_addr);

    // Helper functions for WriteFunction
    DataPacket dequeueDataPacket(); // Dequeues a data packet for sending
    void sendDataToSubscribers(const DataPacket& packet); // Sends data to all subscribers
    void preparePacketBuffer(const DataPacket& packet, char* buf); // Prepares the packet buffer
    void logPacketSend(const Subscriber& subscriber); // Logs packet sending information

    // Data queues and lists
    std::queue<DataPacket> data_queue; // Queue of data packets to send
    std::vector<Subscriber> subscribers; // List of active subscribers
    std::vector<Subscriber> rogue_data_centers; // List of rogue data centers

    // Constants
    const std::string PASSWORD = "Leaf"; // Authentication password
    const int ROGUE_THRESHOLD = 3; // Threshold for rogue activity detection
};


#endif 