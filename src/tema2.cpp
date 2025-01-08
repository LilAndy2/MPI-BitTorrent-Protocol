#include <mpi.h>
#include <fstream>
#include <pthread.h>
#include <iostream>
#include <cstring>
#include <vector>
#include <mutex>
#include <unordered_map>

using namespace std;

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100
#define MESSAGE_SIZE 100

// Define tags for MPI messages in different contexts
#define TRACKER_SEND_DATA 0
#define TRACKER_INITIAL_REQUEST 1
#define CONTACT_UPLOAD 2
#define UPDATE_PEER_LIST 3
#define DONE_DOWNLOAD 4
#define REQUEST_SEGMENT 5
#define SEND_SEGMENT 6

typedef struct {
    string filename;
    vector<string> segments;
    int num_segments;
} File;

typedef struct {
    int num_files;
    vector<File> files;
    int num_wanted_files;
    vector<string> wanted_files;
    int rank;
} Client;

Client client;
std::mutex test_mutex;

void serialize_client(const Client &client, std::vector<char> &buffer) {
    int offset = 0;
    int size = sizeof(client.num_files) +
               sizeof(client.num_wanted_files) +
               sizeof(client.rank) +
               client.files.size() * (sizeof(int) + MAX_FILENAME) +
               client.wanted_files.size() * MAX_FILENAME +
               client.num_files * MAX_CHUNKS * HASH_SIZE;

    buffer.resize(size);

    memcpy(buffer.data() + offset, &client.num_files, sizeof(client.num_files));
    offset += sizeof(client.num_files);

    for (const auto &file : client.files) {
        int filename_length = file.filename.size();
        memcpy(buffer.data() + offset, &filename_length, sizeof(filename_length));
        offset += sizeof(filename_length);

        memcpy(buffer.data() + offset, file.filename.c_str(), filename_length);
        offset += filename_length;

        memcpy(buffer.data() + offset, &file.num_segments, sizeof(file.num_segments));
        offset += sizeof(file.num_segments);

        for (const auto &segment : file.segments) {
            memcpy(buffer.data() + offset, segment.c_str(), HASH_SIZE);
            offset += HASH_SIZE;
        }
    }

    memcpy(buffer.data() + offset, &client.num_wanted_files, sizeof(client.num_wanted_files));
    offset += sizeof(client.num_wanted_files);

    for (const auto &wanted_file : client.wanted_files) {
        int wanted_file_length = wanted_file.size();
        memcpy(buffer.data() + offset, &wanted_file_length, sizeof(wanted_file_length));
        offset += sizeof(wanted_file_length);

        memcpy(buffer.data() + offset, wanted_file.c_str(), wanted_file_length);
        offset += wanted_file_length;
    }

    memcpy(buffer.data() + offset, &client.rank, sizeof(client.rank));
}

void deserialize_client(Client &client, const std::vector<char> &buffer) {
    int offset = 0;

    memcpy(&client.num_files, buffer.data() + offset, sizeof(client.num_files));
    offset += sizeof(client.num_files);

    client.files.resize(client.num_files);

    for (int i = 0; i < client.num_files; ++i) {
        int filename_length;
        memcpy(&filename_length, buffer.data() + offset, sizeof(filename_length));
        offset += sizeof(filename_length);

        client.files[i].filename.assign(buffer.data() + offset, filename_length);
        offset += filename_length;

        memcpy(&client.files[i].num_segments, buffer.data() + offset, sizeof(client.files[i].num_segments));
        offset += sizeof(client.files[i].num_segments);

        client.files[i].segments.resize(client.files[i].num_segments);

        for (int j = 0; j < client.files[i].num_segments; ++j) {
            client.files[i].segments[j].assign(buffer.data() + offset, HASH_SIZE);
            offset += HASH_SIZE;
        }
    }

    memcpy(&client.num_wanted_files, buffer.data() + offset, sizeof(client.num_wanted_files));
    offset += sizeof(client.num_wanted_files);

    client.wanted_files.resize(client.num_wanted_files);

    for (int i = 0; i < client.num_wanted_files; ++i) {
        int wanted_file_length;
        memcpy(&wanted_file_length, buffer.data() + offset, sizeof(wanted_file_length));
        offset += sizeof(wanted_file_length);

        client.wanted_files[i].assign(buffer.data() + offset, wanted_file_length);
        offset += wanted_file_length;
    }

    memcpy(&client.rank, buffer.data() + offset, sizeof(client.rank));
}

// Debugging function to print the client data
void print_client(const Client &client) {
    cout << "Number of files: " << client.num_files << endl;

    // For each file, print the filename, number of segments and the hash of each segment
    for (const auto &file : client.files) {
        cout << "File Name: " << file.filename << endl;
        cout << "Number of Segments: " << file.num_segments << endl;
        for (size_t i = 0; i < file.segments.size(); ++i) {
            cout << "Segment " << (i + 1) << " Hash: " << file.segments[i] << endl;
        }
    }

    // Print the number of wanted files and the name of each wanted file
    cout << "Number of wanted files: " << client.num_wanted_files << endl;
    for (const auto &wanted_file : client.wanted_files) {
        cout << "Wanted File: " << wanted_file << endl;
    }
    cout << "Client Rank: " << client.rank << endl;
}

void read_input_file(int rank) {
    // Read the input file for the client with the given rank
    string filename = "in" + to_string(rank) + ".txt";
    ifstream file(filename);

    if (!file.is_open()) {
        cerr << "Error: Unable to open file " << filename << endl;
        return;
    }

    // Read the number of files and the details for each file
    file >> client.num_files;
    file.ignore();

    for (int i = 0; i < client.num_files; ++i) {
        File f;
        file >> f.filename >> f.num_segments;
        file.ignore();
        for (int j = 0; j < f.num_segments; ++j) {
            string hash;
            file >> hash;
            f.segments.push_back(hash);
        }
        client.files.push_back(f);
    }

    file >> client.num_wanted_files;
    file.ignore();

    for (int i = 0; i < client.num_wanted_files; ++i) {
        string wanted_file;
        file >> wanted_file;
        client.wanted_files.push_back(wanted_file);
    }

    client.rank = rank;

    file.close();
}

void send_initial_data_to_tracker(int rank) {
    // Serialize the client data
    std::vector<char> buffer;
    serialize_client(client, buffer);

     // Send the client data to the tracker
    MPI_Send(buffer.data(), buffer.size(), MPI_CHAR, TRACKER_RANK, TRACKER_SEND_DATA, MPI_COMM_WORLD);
}

void *download_thread_func(void *arg) {
    int rank = *(int*) arg;

    // For each wanted file, request the missing segments from the tracker
    for (const auto &wanted_file : client.wanted_files) {
        //Send the wanted file to the tracker
        MPI_Send(wanted_file.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, TRACKER_INITIAL_REQUEST, MPI_COMM_WORLD);
        
        // Receive the number of peers that have the file
        int num_peers;
        MPI_Recv(&num_peers, 1, MPI_INT, TRACKER_RANK, TRACKER_INITIAL_REQUEST, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
        // Receive the list of peers that have the file
        vector<int> peers(num_peers);
        if (num_peers > 0) {
            MPI_Recv(peers.data(), num_peers, MPI_INT, TRACKER_RANK, TRACKER_INITIAL_REQUEST, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }

        // Receive the number of missing segments
        int num_segments;
        MPI_Recv(&num_segments, 1, MPI_INT, TRACKER_RANK, TRACKER_INITIAL_REQUEST, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // Receive the list of missing segments
        char missing_hashes[MAX_CHUNKS][HASH_SIZE + 1];
        MPI_Recv(missing_hashes, num_segments * (HASH_SIZE + 1), MPI_CHAR, TRACKER_RANK, TRACKER_INITIAL_REQUEST, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
        // Convert the missing segments to a vector of strings for simplicity
        vector<string> missing_segments(num_segments);
        for (int i = 0; i < num_segments; ++i) {
            missing_segments[i] = std::string(missing_hashes[i]);
        }

        // Create a new file entry for the wanted file
        File new_file;
        new_file.filename = wanted_file;
        new_file.num_segments = num_segments;

        // Add the new file to the client's list of files
        // Use a mutex to protect the shared data structure
        test_mutex.lock();
        client.files.push_back(new_file);
        File &file = client.files.back();
        test_mutex.unlock();

        int num_segments_downloaded = 0;
        srand(static_cast<unsigned int>(time(NULL)));
        for (const auto &segment : missing_segments) {
            // Choose a random peer to download the segment from
            int peer = peers[rand() % num_peers];
            char response[MESSAGE_SIZE];
            strcpy(response, "NACK");

            // Keep trying to download the segment until a one of the peers has it
            while (strcmp(response, "NACK") == 0 || peer == 0 || peer == rank) {
                MPI_Send(wanted_file.c_str(), MAX_FILENAME + 1, MPI_CHAR, peer, CONTACT_UPLOAD, MPI_COMM_WORLD);
                MPI_Send(segment.c_str(), HASH_SIZE + 1, MPI_CHAR, peer, REQUEST_SEGMENT, MPI_COMM_WORLD);
                MPI_Recv(response, MESSAGE_SIZE, MPI_CHAR, peer, SEND_SEGMENT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                int peer = peers[rand() % num_peers];
            }

            // Add the downloaded segment to the file
            char segment_hash[HASH_SIZE + 1];
            strncpy(segment_hash, segment.c_str(), HASH_SIZE);
            segment_hash[HASH_SIZE] = '\0';

            // Use a mutex to protect the shared data structure
            test_mutex.lock();
            file.segments.push_back(segment_hash);
            test_mutex.unlock();

            num_segments_downloaded++;

            // Periodically update the tracker with the list of peers that have the file
            if (num_segments_downloaded % 10 == 0) {
                MPI_Send(wanted_file.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, UPDATE_PEER_LIST, MPI_COMM_WORLD);
            }
        }

        // Write the downloaded file
        ofstream output_file("client" + to_string(rank) + "_" + wanted_file);
        test_mutex.lock();
        for (const auto &segment : file.segments) {
            output_file << segment << endl;
        }
        test_mutex.unlock();
    }

    // Notify the tracker that the download is complete
    MPI_Send("DONE", MESSAGE_SIZE, MPI_CHAR, TRACKER_RANK, DONE_DOWNLOAD, MPI_COMM_WORLD);

    return NULL;
}

void *upload_thread_func(void *arg) {
    int rank = *(int*) arg;

    // Wait for requests from other peers to upload segments
    while (true) {
        char request_filename[MAX_FILENAME + 1];
        MPI_Status status;

        MPI_Recv(request_filename, MESSAGE_SIZE, MPI_CHAR, MPI_ANY_SOURCE, CONTACT_UPLOAD, MPI_COMM_WORLD, &status);
        // Check if the tracker has sent a stop message
        if (strcmp(request_filename, "Stop") == 0 && status.MPI_SOURCE == TRACKER_RANK) {
            // If so, break out of the loop
            break;
        }

        // Receive the hash of the requested segment
        char requested_hash[HASH_SIZE + 1];
        MPI_Recv(requested_hash, HASH_SIZE + 1, MPI_CHAR, status.MPI_SOURCE, REQUEST_SEGMENT, MPI_COMM_WORLD, &status);
        
        // Check if the requested segment is available
        // Use a mutex to protect the shared data structure
        test_mutex.lock();
        bool found = false;
        for (const auto &file : client.files) {
            if (file.filename == request_filename) {
                for (const auto &segment : file.segments) {
                    if (strcmp(segment.c_str(), requested_hash) == 0) {
                        found = true;
                        break;
                    }
                }

                if (found) {
                    break;
                }
            }
        }
        test_mutex.unlock();

        // Send the response to the peer that requested the segment
        char response[MESSAGE_SIZE];
        if (found) {
            strcpy(response, "ACK");
        } else {
            strcpy(response, "NACK");
        }
        MPI_Send(response, MESSAGE_SIZE, MPI_CHAR, status.MPI_SOURCE, SEND_SEGMENT, MPI_COMM_WORLD);
    }

    return NULL;
}

void tracker(int numtasks, int rank) {
    vector<Client> clients(numtasks - 1);
    int completed_clients = 0;

    // Receive the client data from each peer
    for (int i = 1; i < numtasks; ++i) {
        MPI_Status status;
        MPI_Probe(i, 0, MPI_COMM_WORLD, &status);
        
        int count;
        MPI_Get_count(&status, MPI_CHAR, &count);
        
        // Receive the serialized client data
        vector<char> buffer(count);
        MPI_Recv(buffer.data(), count, MPI_CHAR, i, TRACKER_SEND_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
        // Deserialize the client data
        deserialize_client(clients[i - 1], buffer);
    }

    // Create a map which associates each file with the peers that have it
    unordered_map<string, vector<int>> file_peers;

    // Populate the map with the files and the peers that have them
    for (const auto &client : clients) {
        for (const auto &file : client.files) {
            file_peers[file.filename].push_back(client.rank);
        }
    }

    // After receveing all the clients' data, notify all peers to start downloading
    for (int i = 1; i < numtasks; i++) {
        MPI_Send("START DOWNLOAD", MESSAGE_SIZE, MPI_CHAR, i, TRACKER_SEND_DATA, MPI_COMM_WORLD);
    }

    // Wait for the peers to finish downloading
    bool running = true;
    while (running) {
        // Check for incoming messages from the peers
        MPI_Status status;
        MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

        // Process the incoming message based on the tag
        if (status.MPI_TAG == TRACKER_INITIAL_REQUEST) {
            // The peer is requesting the list of peers that have the file
            char filename[100];
            MPI_Recv(filename, 100, MPI_CHAR, status.MPI_SOURCE, TRACKER_INITIAL_REQUEST, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Send the list of peers that have the file
            string file_str(filename);
            vector<int> peers;
            for (int j = 1; j < numtasks; j++) {
                if (j != status.MPI_SOURCE) {
                    for (const auto &entry : file_peers) {
                        if (entry.first == file_str) {
                            for (const auto &peer : entry.second) {
                                if (peer != status.MPI_SOURCE) {
                                    peers.push_back(peer);
                                }
                            }
                            break;
                        }
                    }
                }
            }

            // Store the file data in a temporary structure
            File file_tmp;
            for (const auto &entry : file_peers) {
                if (entry.first == file_str) {
                    for (const auto &file : clients[entry.second[0] - 1].files) {
                        if (file.filename == file_str) {
                            file_tmp.filename = file.filename;
                            file_tmp.num_segments = file.num_segments;
                            for (const auto &segment : file.segments) {
                                file_tmp.segments.push_back(segment);
                            }
                            break;
                        }
                    }
                }
            }

            // Send the list of peers that have the file
            int num_peers = peers.size();
            MPI_Send(&num_peers, 1, MPI_INT, status.MPI_SOURCE, TRACKER_INITIAL_REQUEST, MPI_COMM_WORLD);
            if (num_peers > 0) {
                MPI_Send(peers.data(), num_peers, MPI_INT, status.MPI_SOURCE, TRACKER_INITIAL_REQUEST, MPI_COMM_WORLD);
            }

            // Send the number of missing segments
            MPI_Send(&file_tmp.num_segments, 1, MPI_INT, status.MPI_SOURCE, TRACKER_INITIAL_REQUEST, MPI_COMM_WORLD);
            
            // Send the list of missing segments
            char segments[MAX_CHUNKS][HASH_SIZE + 1];
            for (int i = 0; i < file_tmp.num_segments; ++i) {
                strncpy(segments[i], file_tmp.segments[i].c_str(), HASH_SIZE);
                segments[i][HASH_SIZE] = '\0';           
            }

            MPI_Send(segments, file_tmp.num_segments * (HASH_SIZE + 1), MPI_CHAR, status.MPI_SOURCE, TRACKER_INITIAL_REQUEST, MPI_COMM_WORLD);
        } else if (status.MPI_TAG == UPDATE_PEER_LIST) {
            // The peer wants the updated list of peers that have the file
            char filename[100];
            MPI_Recv(filename, 100, MPI_CHAR, status.MPI_SOURCE, UPDATE_PEER_LIST, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Update the list of peers that have the file
            string file_str(filename);
            vector<int> peers;
            for (int j = 1; j < numtasks; j++) {
                for (const auto &entry : file_peers) {
                    if (entry.first == file_str) {
                        file_peers[file_str].push_back(status.MPI_SOURCE);
                        break;
                    }
                }
            }
        } else if (status.MPI_TAG == DONE_DOWNLOAD) {
            // The peer has finished downloading
            char message[MESSAGE_SIZE];
            MPI_Recv(&message, MESSAGE_SIZE, MPI_CHAR, status.MPI_SOURCE, DONE_DOWNLOAD, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (strcmp(message, "DONE") == 0) {
                completed_clients++;
            }

            // Check if all peers have finished downloading
            if (completed_clients == numtasks - 1) {
                running = false;
            }
        }
    } 

    // Notify all peers to stop uploading
    for (int i = 1; i < numtasks; i++) {
        MPI_Send("Stop", MESSAGE_SIZE, MPI_CHAR, i, CONTACT_UPLOAD, MPI_COMM_WORLD);
    }
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    // Read the input file for the client and send the data to the tracker
    read_input_file(rank);
    send_initial_data_to_tracker(rank);

    // Wait for the tracker to send the start download message
    char message[MESSAGE_SIZE];
    MPI_Recv(&message, MESSAGE_SIZE, MPI_CHAR, TRACKER_RANK, TRACKER_SEND_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // Start the download and upload threads
    if (strcmp(message, "START DOWNLOAD") == 0) {
        r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &rank);
        if (r) {
            cout << "Eroare la crearea thread-ului de download\n";
            exit(-1);
        }

        r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
        if (r) {
            cout << "Eroare la crearea thread-ului de upload\n";
            exit(-1);
        }

        r = pthread_join(download_thread, &status);
        if (r) {
            cout << "Eroare la asteptarea thread-ului de download\n";
            exit(-1);
        }

        r = pthread_join(upload_thread, &status);
        if (r) {
            cout << "Eroare la asteptarea thread-ului de upload\n";
            exit(-1);
        }
    }
}

int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    // Initialize MPI with multi-threading support
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        cout << "MPI nu are suport pentru multi-threading\n";
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}