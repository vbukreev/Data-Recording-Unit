#include "DataAcquisition.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <signal.h>

std::atomic<bool> keepRunning(true);

void signalHandler(int signum) {
    
    std::cout << "Interrupt signal (" << signum << ") received.\n";
    keepRunning = false;
}

int main() {
      struct sigaction action;
    action.sa_handler = signalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    
    DataAcquisitionUnit daqUnit;
    daqUnit.start();

    while (keepRunning) {
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    
    daqUnit.stop();
    std::cout << "Data acquisition unit stopped gracefully.\n";

    return 0;
}