// DataAcquisitionUnit.cpp
#include "DataAcquisition.h"
#include "SeismicData.h"
#include <iostream>
#include <unistd.h> 
#include <deque> 
#include <string>
#include <mutex>
#include <thread>
#include <netinet/in.h>
#include <sys/socket.h> 
#include <cstring>
#include <arpa/inet.h>
#include <sstream> 
#include <algorithm> 
#include <semaphore.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <cstring>
#include <cerrno>

const char *semName = SEMNAME;
const char *shmName = MEMNAME;



DataAcquisitionUnit::DataAcquisitionUnit() : running(false), sem_id(SEM_FAILED), shm_fd(-1), shm_ptr(nullptr) {
    // Open the existing semaphore
    sem_id = sem_open(SEMNAME, 0); // Do not use O_CREAT
    if (sem_id == SEM_FAILED) {
        std::cerr << "DataAcquisitionUnit: sem_open() error: " << std::strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }

    // Obtain the shared memory key
    key_t ShmKey = ftok(MEMNAME, 65);
    if (ShmKey == -1) {
        std::cerr << "DataAcquisitionUnit: ftok() error: " << std::strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }

    // Get the shared memory ID
    shm_fd = shmget(ShmKey, sizeof(SeismicMemory), 0666); // Do not use IPC_CREAT
    if (shm_fd < 0) {
        std::cerr << "DataAcquisitionUnit: shmget() error: " << std::strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }

    // Attach the shared memory segment
    shm_ptr = (SeismicMemory *)shmat(shm_fd, NULL, 0);
    if (shm_ptr == (void *)-1) {
        std::cerr << "DataAcquisitionUnit: shmat() error: " << std::strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }
}

DataAcquisitionUnit::~DataAcquisitionUnit() {
    stop();
}

void DataAcquisitionUnit::start() {
    running = true;
    readThread = std::thread(&DataAcquisitionUnit::readThreadFunction, this);
    writeThread = std::thread(&DataAcquisitionUnit::writeThreadFunction, this);
}

void DataAcquisitionUnit::stop() {
    running = false;
    if (readThread.joinable()) readThread.join();
    if (writeThread.joinable()) writeThread.join();
}



void DataAcquisitionUnit::readSeismicData() {
    // Wait for the semaphore to be available
    if (sem_wait(sem_id) == -1) {
        if (errno == EINTR) {}
        std::cerr << "DataAcquisitionUnit: sem_wait() error: " << std::strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }

    // Check if the shared memory status is WRITTEN
    if (shm_ptr->seismicData[shm_ptr->packetNo % NUM_DATA].status == WRITTEN) {
        // Lock the queue mutex before pushing data to the queue
        std::lock_guard<std::mutex> guard(queueMutex);

        // Construct the DataPacket and push it onto the queue
        SeismicData& seismicData = shm_ptr->seismicData[shm_ptr->packetNo % NUM_DATA];
        DataPacket packet;
        packet.packetNo = shm_ptr->packetNo;
        packet.packetLen = seismicData.packetLen;
        memcpy(packet.data, seismicData.data, seismicData.packetLen);
        

        dataQueue.push_back(packet);

        // Set the shared memory status to READ
        seismicData.status = READ;
        std::cout << "Real-time process: Data read from shared memory. Packet No: " << packet.packetNo << std::endl;
    }

    // Sleep for 1 second after reading from shared memory
    sleep(1);
}


void DataAcquisitionUnit::readThreadFunction() {
    // Setup socket for receiving subscription requests
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servaddr, cliaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

    servaddr.sin_family = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(1153); // Port number

    bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr));
    
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    while (running) {
        char buffer[1024];
        unsigned int len = sizeof(cliaddr);
        int n = recvfrom(sockfd, (char *)buffer, 1024, MSG_WAITALL, (struct sockaddr *)&cliaddr, &len);
        
         if (n > 0) {
             buffer[n] = '\0';
             std::string message(buffer);
             
              // Handle subscription and unsubscription requests
         if (message.find("Subscribe") != std::string::npos) {
             
             std::istringstream ss(message);
             std::string token, username, password;
             std::getline(ss, token, ','); 
             std::getline(ss, username, ',');
             std::getline(ss, password, ',');
             if (password == "Leaf") { // Check password
                 std::string ipAddress = inet_ntoa(cliaddr.sin_addr);
                 int port = ntohs(cliaddr.sin_port);
                 std::lock_guard<std::mutex> guard(queueMutex);
                 subscribers.push_back(Subscriber(username, ipAddress, port));
                 std::cout << "username:" << username << " Subscribed!" << std::endl;
                 std::cout.flush();
                 // Send back to the data center
                const char* msg = "Subscribed";
                sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr *)&cliaddr, len);
             }
         } else if (message.find("Cancel") != std::string::npos) {
             std::istringstream ss(message);
             std::string token, username;
             std::getline(ss, token, ',');
             std::getline(ss, username, ',');
             auto it = std::find_if(subscribers.begin(), subscribers.end(),
                                    [&username](const Subscriber& s) { return s.username == username; });
             if (it != subscribers.end()) {
                 subscribers.erase(it);
                 std::cout << "Subscriber removed: " << username << std::endl;
             }
         } 
         else {
             // Implement protection against rogue data centers
             std::string ipAddress = inet_ntoa(cliaddr.sin_addr);
             ++ipAttempts[ipAddress];
             if (ipAttempts[ipAddress] >= 3) {
                 std::cout << "Adding " << ipAddress << " to the rogue client list" << std::endl;
                 std::cout.flush();
                 ipAttempts.erase(ipAddress); 
             }
         }
             
             
}
            
    readSeismicData();
    usleep(10000);
       
    }
    close(sockfd);
}

void DataAcquisitionUnit::writeThreadFunction() {
    // Setup socket for sending data to subscribed data centers
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in cliaddr;
    memset(&cliaddr, 0, sizeof(cliaddr));

    while (running) {
        std::lock_guard<std::mutex> guard(queueMutex);
        while (!dataQueue.empty()) {
            DataPacket packet = dataQueue.front();
            dataQueue.pop_front();

            // Send packet to each subscribed data center
            for (auto &subscriber : subscribers) {
                cliaddr.sin_family = AF_INET;
                cliaddr.sin_port = htons(subscriber.port);
                inet_pton(AF_INET, subscriber.ipAddress.c_str(), &cliaddr.sin_addr);

                // Construct the packet with packet number, length, and data
                unsigned char sendBuffer[2 + 1 + BUF_LEN];
                sendBuffer[0] = packet.packetNo >> 8;
                sendBuffer[1] = packet.packetNo & 0xFF;
                sendBuffer[2] = packet.packetLen;
                memcpy(&sendBuffer[3], packet.data, packet.packetLen);

                // Send the constructed packet
                sendto(sockfd, sendBuffer, 3 + packet.packetLen, MSG_CONFIRM, (const struct sockaddr *)&cliaddr, sizeof(cliaddr));
                
               std::cout << "send_func: " << inet_ntoa(cliaddr.sin_addr) << ":" << ntohs(cliaddr.sin_port) << std::endl;
               std::cout.flush();
              std::cout << "dataPacket.size():" << packet.packetLen << " client.size():" << subscribers.size() << std::endl;
              std::cout.flush();
              std::cout << "Real-time process: Data sent to subscriber. IP: " << inet_ntoa(cliaddr.sin_addr) << " Port: " << ntohs(cliaddr.sin_port) << std::endl;
            }
        }
        // Sleep for 1 second after sending all packets
        sleep(1);
    }
    close(sockfd);
}