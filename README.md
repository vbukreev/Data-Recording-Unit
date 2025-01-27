# Transducer: Seismic Data Processing and Distribution System

Transducer is a robust system designed to process, transmit, and monitor seismic activity data in real-time. It incorporates multithreading, inter-process communication, and network programming to ensure seamless data handling and secure distribution across multiple data centers.

## Key Features
1. **Seismic Data Acquisition**:
   - The `Transducer` module generates and stores seismic activity data in shared memory.
   - Utilizes semaphores for safe synchronization across processes.

2. **Real-Time Data Distribution**:
   - Data is transmitted to subscribed `Data Centers` over a UDP socket connection.
   - Subscriptions are authenticated using secure credentials.

3. **Rogue Activity Detection**:
   - Monitors and detects rogue `Data Centers` attempting unauthorized access.
   - Automatically blocks rogue entities after multiple failed attempts.

4. **Multithreading**:
   - Enables concurrent data processing and distribution to optimize performance.

## How It Works
### Transducer:
- Continuously generates random seismic data and writes it to shared memory.
- Synchronizes data using semaphores and signals for safe concurrent access.

### Data Centers:
- Subscribes to the seismic data stream.
- Logs and processes received packets in real-time.

### Rogue Data Centers:
- Attempts to brute-force the subscription password.
- Automatically detected and blocked by the `Data Acquisition` unit based on a predefined rogue activity threshold.

## Outputs
- **Subscribed Data Centers**: Receive seismic data packets with detailed logs for each transmission.
- **Rogue Detection**: Outputs security logs identifying and blocking malicious entities.
- **System Logs**: Comprehensive real-time logs for debugging and analysis.

## Project Structure
- **Transducer**: Generates and stores seismic data.
- **Data Acquisition**: Manages data subscriptions, transmission, and rogue activity detection.
- **Data Centers**: Receives and processes data.
- **Rogue Data Centers**: Simulates malicious attempts to access the system.

## Technologies and Concepts
- **Multithreading**: Efficiently handles concurrent data processing tasks.
- **Inter-Process Communication (IPC)**: Uses shared memory and semaphores for data synchronization.
- **Networking**: Implements UDP sockets for data transmission.
- **Signal Handling**: Graceful shutdowns and process control using signals.

This project demonstrates expertise in systems programming, real-time data processing, and secure network communication.
