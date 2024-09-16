#include "DataAcquisition.h"
#include <sstream>      
#include <cstring>


struct sigaction action;

void *recv_func(void *arg);
void *write_func(void *arg);

DataAcquisition* DataAcquisition::instance=nullptr;



DataAcquisition::DataAcquisition() {
    is_running=true;
    ShmPTR=nullptr;
    DataAcquisition::instance=this;
}

DataAcquisition::~DataAcquisition() {
    shmdt((void *) ShmPTR);
    shmctl(ShmID, IPC_RMID, NULL);
    
    close(sockfd);
    
    sem_close(sem_id1);
    sem_unlink(SEMNAME);
    
}

static void handleSigintShutdown(int signum) {
    if (signum == SIGINT) {
        DataAcquisition::instance->shutdown();
    }
}

void setupSigintHandler() {
    action.sa_handler = handleSigintShutdown;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, NULL);
}


void DataAcquisition::shutdown() {
    is_running=false;
}


int DataAcquisition::SharedMemory() {
    ShmKey = ftok(MEMNAME, 65);
    if (ShmKey == -1) {
        strerror(errno);

    }
    ShmID = shmget(ShmKey, sizeof(struct SeismicMemory), IPC_CREAT | 0666);
    if (ShmID < 0) {
        cout<<"Error"<<endl;
        cout<<strerror(errno)<<endl;
        return -1;
    }
    ShmPTR = (struct SeismicMemory *) shmat(ShmID, NULL, 0);
    if (ShmPTR == (void *)-1) {
        cout<<"Error"<<endl;
        cout<<strerror(errno)<<endl;
        return -1;
    }

    return 0;
}

//Semaphores
void DataAcquisition::semInitialize() {
    sem_id1 = sem_open(SEMNAME, O_CREAT, SEM_PERMS, 0);
    if (sem_id1 == SEM_FAILED) {
        std::cerr << "Error: sem_open() failed" << std::endl;
        exit(EXIT_FAILURE);
    }
}

void DataAcquisition::initializeUdp() {
    sockfd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (sockfd < 0) {
        std::cerr << "Error" << std::endl;
        exit(EXIT_FAILURE);
    }
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(1153);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        std::cerr << "Error" << std::endl;
        exit(EXIT_FAILURE);
    }

    if (bind(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error" << std::endl;
        exit(EXIT_FAILURE);
    }
}


void DataAcquisition::SubscriberAdd(const std::string& username, const sockaddr_in& client_addr) {
     uint16_t port = ntohs(client_addr.sin_port);
    subscribers.emplace_back(username, client_addr, port);
}

//remove subscriber
void DataAcquisition::removeSubscriber(const sockaddr_in& client_addr) {
    for (auto it = subscribers.begin(); it != subscribers.end(); ++it) {
        if (it->address.sin_family == client_addr.sin_family &&
            it->address.sin_port == client_addr.sin_port &&
            it->address.sin_addr.s_addr == client_addr.sin_addr.s_addr) {
            subscribers.erase(it);
            break;  // Exit the loop after removing the subscriber
        }
    }
}


void DataAcquisition:: updateRogue(const std::string& username, const sockaddr_in& client_addr ){
    rogue_data_centers.emplace_back(username, client_addr, ntohs(client_addr.sin_port));    
}


void DataAcquisition::executeData() {
    initializeSystemComponents();
    startDataThreads();

    processDataLoop();

    
}

void DataAcquisition::initializeSystemComponents() {
    handleSigintShutdown(SIGINT);
    SharedMemory();
    semInitialize();
    initializeUdp();
}

void DataAcquisition::startDataThreads() {
    pthread_create(&read_thread, NULL, recv_func, (void *) this);
    pthread_create(&write_thread, NULL, write_func, (void *) this);
}

void DataAcquisition::processDataLoop() {
    int seismicDataIndex = 0;
    while (is_running) {
        sem_wait(sem_id1);

        if (ShmPTR->seismicData[seismicDataIndex].status == WRITTEN) {
            DataPacket packet = createDataPacket(seismicDataIndex);
            ShmPTR->seismicData[seismicDataIndex].status = READ;
            sem_post(sem_id1);

            addToDataQueue(packet);

            seismicDataIndex = updateIndex(seismicDataIndex);
        } else {
            sem_post(sem_id1);
        }

        sleep(1);
    }
}

inline DataAcquisition::DataPacket DataAcquisition::createDataPacket(int index) {
    DataPacket packet;
    packet.packet_number = ShmPTR->packetNo;
    packet.packet_length = ShmPTR->seismicData[index].packetLen;
    packet.data = ShmPTR->seismicData[index];
    return packet;
}

void DataAcquisition::addToDataQueue(const DataPacket& packet) {
    pthread_mutex_lock(&lock_x);
    data_queue.push(packet);
    pthread_mutex_unlock(&lock_x);
}

int DataAcquisition::updateIndex(int currentIndex) {
    currentIndex++;
    if (currentIndex >= NUM_DATA) {
        currentIndex = 0;
    }
    return currentIndex;
}

void DataAcquisition::shutdownDataAcquisition() {
    cout << "DataAcquisition Unit: Shutting down..." << endl;
    pthread_join(read_thread, NULL);
    pthread_join(write_thread, NULL);
    close(sockfd);
    sem_close(sem_id1);
    sem_unlink(SEMNAME);
    shmdt((void *) ShmPTR);
    shmctl(ShmID, IPC_RMID, NULL);
    pthread_mutex_destroy(&lock_x);
    exit(EXIT_SUCCESS);
}


void *recv_func(void *arg)
{
    DataAcquisition *data_acquisition = (DataAcquisition *) arg;
    data_acquisition->receiveFunction();
    pthread_exit(NULL);
}


void *write_func(void *arg)
{
    DataAcquisition *data_acquisition = (DataAcquisition *) arg;
    data_acquisition->WriteFunction();
    pthread_exit(NULL);
}

//Recieve Function and helper functions to manage the recieved data
void DataAcquisition::receiveFunction() {
    std::map<std::string, int> ip_connection_count;
    char buffer[BUF_LEN];
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);



     processDataLoop(); 
    int seismicDataIndex = 0;
    while (is_running) {
        sem_wait(sem_id1);

        if (ShmPTR->seismicData[seismicDataIndex].status == WRITTEN) {
            DataPacket packet = createDataPacket(seismicDataIndex);
            ShmPTR->seismicData[seismicDataIndex].status = READ;
            sem_post(sem_id1);

            addToDataQueue(packet);

            seismicDataIndex = updateIndex(seismicDataIndex);
        } else {
            sem_post(sem_id1);
        }

        sleep(1);
    }
}



void DataAcquisition::checkForRogueActivity(const std::string& username, const sockaddr_in& client_addr, const std::string& ip_and_port, std::map<std::string, int>& ip_connection_count) {
    if (ip_connection_count[ip_and_port] > ROGUE_THRESHOLD) {
        cout << "Adding " << ip_and_port << " to the rogue client list." << endl;
        pthread_mutex_lock(&lock_x);
        updateRogue(username, client_addr); 
        removeSubscriber(client_addr);
        pthread_mutex_unlock(&lock_x);
        ip_connection_count[ip_and_port] = 0;
    }
}

void DataAcquisition::processReceivedData(char* buffer, const sockaddr_in& client_addr, std::map<std::string, int>& ip_connection_count) {
    if (isRogueClient(client_addr)) {
        return;
    }

    std::string ip_and_port = getClientIpAndPort(client_addr);
    ip_connection_count[ip_and_port]++;

    char actionString[256], username[256], password[256];
    parseBuffer(buffer, actionString, username, password);

    if (strcmp(actionString, "Subscribe") == 0 && strcmp(password, PASSWORD.c_str()) == 0) {
    handleSubscription(username, client_addr, ip_and_port, ip_connection_count);
    
    } else if (strcmp(actionString, "Cancel") == 0) {
        handleCancellation(username, client_addr, ip_and_port, ip_connection_count);
    } else {
        cout << "DataAcquisition: unknown command " << actionString << endl;
    }

    checkForRogueActivity(username, client_addr, ip_and_port, ip_connection_count);
}

bool DataAcquisition::isRogueClient(const sockaddr_in& client_addr) {
    uint16_t client_port = ntohs(client_addr.sin_port);
    for (const auto& it : rogue_data_centers) {
        if (it.port == client_port) {
            return true;
        }
    }
    return false;
}

std::string DataAcquisition::getClientIpAndPort(const sockaddr_in& client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    return std::string(client_ip) + ":" + std::to_string(ntohs(client_addr.sin_port));
}

void DataAcquisition::parseBuffer(char* buffer, char* actionString, char* username, char* password) {
    char *save_ptr;
    strncpy(actionString, strtok_r(buffer, ",", &save_ptr), sizeof(actionString) - 1);
    strncpy(username, strtok_r(NULL, ",", &save_ptr), sizeof(username) - 1);

    if (strcmp(actionString, "Subscribe") == 0) {
        char *password_token = strtok_r(NULL, ",", &save_ptr);
        strncpy(password, password_token ? password_token : "", sizeof(password) - 1);
    } else {
        password[0] = '\0';
    }
}

void DataAcquisition::handleSubscription(const std::string& username, const sockaddr_in& client_addr, const std::string& ip_and_port, std::map<std::string, int>& ip_connection_count) {
    if (!isAlreadySubscribed(client_addr)) {
        const char data[] = "Subscribed";
        sendto(sockfd, data, sizeof(data), 0, (struct sockaddr *) &client_addr, sizeof(client_addr));
        pthread_mutex_lock(&lock_x);
        SubscriberAdd(username, client_addr);
        pthread_mutex_unlock(&lock_x);
        cout << username << " Subscribed!" << endl;
    }
}

void DataAcquisition::handleCancellation(const std::string& username, const sockaddr_in& client_addr, const std::string& ip_and_port, std::map<std::string, int>& ip_connection_count) {
    pthread_mutex_lock(&lock_x);
    removeSubscriber(client_addr);
    pthread_mutex_unlock(&lock_x);
    cout << username << " Cancelled" << endl;
    ip_connection_count[ip_and_port] = 0;
}



bool DataAcquisition::isAlreadySubscribed(const sockaddr_in& client_addr) {
    for (const auto& subscriber : subscribers) {
        if (subscriber.port == ntohs(client_addr.sin_port)) {
            cout << subscriber.username << " has already Subscribed!!" << endl;
            return true;
        }
    }
    return false;
}

//Write function

void DataAcquisition::WriteFunction() {
    while (is_running) {
        if (data_queue.empty()) {
            usleep(10000); // Sleep for 10ms to reduce CPU usage
            continue;
        }

        DataPacket packet = dequeueDataPacket();
        sendDataToSubscribers(packet);
        sleep(1); // Sleep for 1 second before sending the next packet
    }
}

inline DataAcquisition::DataPacket DataAcquisition::dequeueDataPacket() {
    pthread_mutex_lock(&lock_x);
    DataPacket packet = data_queue.front();
    data_queue.pop();
    pthread_mutex_unlock(&lock_x);
    return packet;
}

void DataAcquisition::sendDataToSubscribers(const DataPacket& packet) {
    char buf[BUF_LEN] = {0};
    preparePacketBuffer(packet, buf);

    for (const Subscriber& subscriber : subscribers) {
        int ret = sendto(sockfd, buf, BUF_LEN, 0, (struct sockaddr*)&subscriber.address, sizeof(subscriber.address));
        if (ret < 0) {
            cerr << "Error: sendto() failed for subscriber " << subscriber.username
                 << ". Error message from strerror: " << strerror(errno) << endl;
        } else {
            logPacketSend(subscriber);
        }
    }
}

void DataAcquisition::preparePacketBuffer(const DataPacket& packet, char* buf) {
    buf[0] = packet.packet_number & 0xFF;
    buf[1] = (packet.packet_length >> 8) & 0xFF;
    buf[2] = packet.packet_length & 0xFF;
    memcpy(buf + 3, packet.data.data, packet.packet_length);
}

void DataAcquisition::logPacketSend(const Subscriber& subscriber) {
    char subscriber_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(subscriber.address.sin_addr), subscriber_ip, INET_ADDRSTRLEN);
    cout << "Send func: " << subscriber_ip << ": " << ntohs(subscriber.address.sin_port) << endl;
    cout << "dataPacket.size(): " << data_queue.size() << " client.size(): " << subscribers.size() << endl;
}