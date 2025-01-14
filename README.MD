# BitTorrent Simulation using MPI and Pthreads

## Overview
This project simulates a simplified version of the BitTorrent protocol using **MPI (Message Passing Interface)** and **Pthreads**. It allows multiple peers to exchange file segments while a tracker manages the distribution.

## Features
- **Tracker-based peer coordination**
- **File segmentation and exchange between peers**
- **Multithreading for simultaneous upload/download**
- **Dynamic peer list updates**
- **Efficient file distribution simulation**

## Components
1. **Tracker (Rank 0)**
   - Maintains a list of peers that have each file.
   - Responds to peer requests for available sources.
   - Updates the peer list dynamically.
   - Signals peers to start downloading.

2. **Peers (Ranks > 0)**
   - Read their available files and desired files from an input file.
   - Communicate with the tracker to obtain peer lists.
   - Use **multithreading** to handle concurrent downloads and uploads.
   - Notify the tracker upon completion.

## Code Structure
- `tracker()`: Implements tracker logic. Gets the initial data from the clients and stores it locally. Checks for incoming messages and sorts them based on their tag. Gives adequate responses to the peers.
- `peer()`: Implements peer logic. Reads the input file for the client and sends the initial data to the tracker. Implements the two types of threads for the peers: download and upload.
- `serialize_client() / deserialize_client()`: Handles serialization for sending client data.
- `download_thread_func()`: Thread function to handle downloading. Checks each wanted file of the client and makes request to the tracker and to the other clients to receive the missing segments. Selects a peer to get the missing segments from. Creates the output files.
- `upload_thread_func()`: Thread function to handle uploading. Waits for requests from other peers to upload segemnts. Sends responses to these peers.
- `read_input_file()`: Reads initial peer file data.
- `send_initial_data_to_tracker()`: Sends initial data to tracker.
- `main()`: Initializes MPI, assigns roles, and starts execution.

## Prerequisites
- MPI Library (e.g., OpenMPI)
- GCC/G++ Compiler
- pthreads Library

## Compilation
Use the following command to compile the program:
```sh
mpic++ -pthread -Wall -o tema2 tema2.cpp
```

## Running the Simulation
Launch the program using MPI:
```sh
mpirun -np <num_processes> ./tema2
```
where `<num_processes>` is the total number of processes (1 tracker + N peers).

## Input Format
Each peer reads an input file named `in<rank>.txt`, formatted as follows:
```
<num_files>
<filename> <num_segments>
<segment_hash_1>
<segment_hash_2>
...
<num_wanted_files>
<wanted_filename>
...
```

## Example Execution
```sh
mpirun -np 4 ./tema2
```
This will start 1 tracker and 3 peers.

## Notes
- Peers exchange file segments randomly from available sources.
- The tracker sends periodic updates to improve efficiency.

## Authors
- Developed as a Parallel and Distributed Algorithms course assignment.

## License
MIT License

