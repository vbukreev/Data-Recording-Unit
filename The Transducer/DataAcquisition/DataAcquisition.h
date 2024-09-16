#pragma once
#include "SeismicData.h"
#include <vector>
#include <mutex>
#include <thread>
#include <map>
#include <deque>
#include <semaphore.h>

struct DataPacket {
    unsigned int packetNo;
    unsigned short packetLen;
    char data[BUF_LEN]; // Ensure BUF_LEN is defined, possibly in SeismicData.h
};

struct Subscriber {
    std::string username;
    std::string ipAddress;
    int port;
    
    Subscriber(const std::string& user, const std::string& ip, int p)
        : username(user), ipAddress(ip), port(p) {}
};

class DataAcquisitionUnit {
public:
    DataAcquisitionUnit();
    ~DataAcquisitionUnit();
    void start();
    void stop();
    void readThreadFunction();
    void writeThreadFunction();
    void readSeismicData();

private:
    std::deque<DataPacket> dataQueue; // Queue for storing seismic data packets
    std::vector<Subscriber> subscribers; // List of subscribers
    std::map<std::string, int> ipAttempts; // Tracking of IP address attempts for rogue detection
    std::mutex queueMutex; // Mutex for synchronizing access to the queue
    std::thread readThread, writeThread; // Threads for reading and writing operations
    bool running; // Flag to control the running state of the threads
    sem_t* sem_id; // Semaphore for synchronizing with the transducer
    int shm_fd; // File descriptor for shared memory
    SeismicMemory* shm_ptr; // Pointer to the shared memory region
};