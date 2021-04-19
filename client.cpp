/*
    Author: Caleb Mills         studentID: 903566199        netID: ctm364
    Date: 04/12/2021
*/

/* COMMENT BLOCK 1: I used poll() to handle my timeouts as that allows
the recv to be nonblocking. This allowed me to enter a loop to handle the
conditions for retransmit and break out once conditions are met
*/

#include "packet.h"

#include <iostream>
#include <sys/types.h>   // defines types (like size_t)
#include <sys/socket.h>  // defines socket class
#include <netinet/in.h>  // defines port numbers for (internet) sockets, some address structures, and constants
#include <netdb.h> 
#include <iostream>
#include <fstream>
#include <arpa/inet.h>   // if you want to use inet_addr() function
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <poll.h>

using namespace std;

// Structure for keeping track of sequence numbers
struct seqTrack {
    int seqNum;
    int seqCycle;
}ns, sb; // next sequence and send base

// log function
void log(int sb, int ns, int outstandingPackets, packet *window){
    cout << "-------------------------------------------------------" << endl;
    window->printContents();
    if(window->getType() == 1){
        cout << "SB: " << sb << endl;
        cout << "NS: " << ns << endl;
        cout << "outstanding packets: " << outstandingPackets << endl;
    }

    else if (window->getType() == 0 || window->getType() == 3)
    {
        cout << "outstanding packets: " << outstandingPackets << endl;
    }
    

    else{
        cout << "EOT Packet Received. Exiting.--------------------------" << endl;
    }
}

int main(int argc, char *argv[]){
    if (argc != 5){
        cout << "Usage: client <servername> <clientSendPort> <clientReceivePort> <filename>" << endl;
        return -1;
    }

    // Send and receive port for the client
    char *sendPort = argv[2];
    char *receivePort = argv[3];

    // Initializing Go-Back-N Variables for client side
    int seqNum = 0; // Current sequence number we are on
    ns.seqNum = seqNum; // Next_Sequence
    sb.seqNum = 0; // Send_Base
    ns.seqCycle = 0; // Cycle to measure modulo passthroughs on Next_Sequence
    sb.seqCycle = 0; // Same as above but for Send_Base
    int windowSize = 7; // The window size for Go-Back-N protocol [0 - 6]
    int index = 0; // Indexing used to keep track of current state
    int totalFrames = 8; // Total frames used to measure possilbe number of sequence numbers
    int finalFrame; // Final frame (sequence number) that the final type 1 packet will be sent on
    int outstandingPackets = 0; // Counter for outstanding packets
    int lastAck = 99;

    bool eot = false; // Variable for end of transmission

    // Opening the files
    ifstream in_file;
    in_file.open(argv[4]);

    ofstream clientSeqNumLog, clientAckLog;
    clientSeqNumLog.open("clientseqnum.log", std::ios::out | std::ios::trunc);
    clientAckLog.open("clientack.log", std::ios::out | std::ios::trunc);

    // Initializing timeout creation
    int ackTimeout = 2000;
    struct pollfd fds[1];

    // Initializing socket creation for client side
    struct hostent *s;
    s = gethostbyname(argv[1]);
    struct sockaddr_in serverSend;
    socklen_t slen = sizeof(serverSend);
    struct sockaddr_in serverReceive;
    int sendSocket = 0;
    int receiveSocket = 0;


    // Creation of the Send socket
    if ((sendSocket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        cout << "Error in creating socket.\n";
    }

    // Send Socket Setup
    memset((char *) &serverSend, 0, sizeof(serverSend));
    serverSend.sin_family = AF_INET;
    serverSend.sin_port = htons(stoi(sendPort));
    bcopy((char *)s->h_addr, 
	(char *)&serverSend.sin_addr.s_addr,
	s->h_length);

    // Creation of receive socket
    if ((receiveSocket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        cout << "Error in creating socket.\n";
    }
    
    // Receive socket setup
    memset((char *) &serverReceive, 0, sizeof(serverReceive));
    serverReceive.sin_family = AF_INET;
    serverReceive.sin_port = htons(stoi(receivePort));
    bcopy((char *)s->h_addr, 
	(char *)&serverReceive.sin_addr.s_addr,
	s->h_length);
    if (bind(receiveSocket, (struct sockaddr *)&serverReceive, sizeof(serverReceive)) == -1){
        cout << "Error in receive binding.\n";
    }

    // Polling variables
    fds[0].fd = receiveSocket;
    fds[0].events = 0;
    fds[0].events |= POLLIN;

    // Initialize the data as a char array of length total frames and another char array of length 31
    // Initialize a chunk to contain the serialized data to be sent over the network
    // Initialize a packet array of the length total frames
    char data[totalFrames][31];
    char chunk[64];
    char chunk2[64];
    packet *window[totalFrames];

    while(!in_file.eof()){
        // if window is  not full
        if (outstandingPackets < windowSize){

            // Initialize sequence number and final frame
            seqNum = (index % totalFrames);
            finalFrame = seqNum;

            memset((char *) &data[seqNum], 0, sizeof(data[seqNum]));
            memset((char *) &chunk, 0, sizeof(chunk));

            // Read 30 characters of the file
            in_file.read(data[seqNum], 30);

            // create a packet
            window[seqNum] = new packet(1, seqNum, in_file.gcount(), data[seqNum]);

            // Send the packet over send socket
            window[seqNum]->serialize(chunk);
            if (sendto(sendSocket, chunk, 64, 0, (struct sockaddr *)&serverSend, slen)==-1){
                cout << "Error in sendto function";
                return -1;
            }

            // log the current sequence number
            clientSeqNumLog << window[seqNum]->getSeqNum() << endl;

            // update the next sequence number
            ns.seqNum = (ns.seqNum + 1) % totalFrames;

            // If we loop around modulo then increase cycle
            if (ns.seqNum == 0){
                ns.seqCycle++;
            }

            // increase outstanding packets and overall index
            outstandingPackets++;
            log(sb.seqNum, ns.seqNum, outstandingPackets, window[seqNum]);
            index++;
        }

        // Else the window is full
        else{

            // Set up polling variables again to clear them
            fds[0].events = 0;
            fds[0].events |= POLLIN;

            // Poll fds variable which includes the receive socket
            // If it doesn't equal zero then we didn't timeout and there is stuff to read
            if(poll(fds, 1, ackTimeout) != 0){
                // call recvfrom now to pull in data on receive socket
                if (recvfrom(receiveSocket, chunk, 64, 0, (struct sockaddr *)&serverReceive, &slen) == -1){
                    cout << "Error in receiving";
                    return -1;
                }

                // create packet for data
                packet *dataPacket = new packet(0, 0, 0, NULL);
                dataPacket->deserialize(chunk);
                    lastAck = dataPacket->getSeqNum();
                    clientAckLog << dataPacket->getSeqNum() << endl;

                    sb.seqNum = (sb.seqNum + 1) % totalFrames;
                    if (sb.seqNum == 0){
                        sb.seqCycle++;
                    }

                    outstandingPackets--;
                    log(sb.seqNum, ns.seqNum, outstandingPackets, dataPacket);
            }

            // Else timeout
            // See COMMENT BLOCK 1 for documentation
            else{
                seqTrack tempSeq = sb;
                while (tempSeq.seqCycle <= ns.seqCycle){
                    if(tempSeq.seqNum < ns.seqNum || tempSeq.seqCycle < ns.seqCycle){
                        memset((char *) &chunk, 0, sizeof(chunk));
                        packet *tempPacket = window[tempSeq.seqNum];
                        tempPacket->serialize(chunk);
                        if (sendto(sendSocket, chunk, 64, 0, (struct sockaddr *)&serverSend, slen)==-1){
                            cout << "Error in sendto function";
                            return -1;
                        }
                        log(sb.seqNum, ns.seqNum, outstandingPackets, tempPacket);
                        tempSeq.seqNum = (tempSeq.seqNum + 1) % totalFrames;
                        if(tempSeq.seqNum == 0){
                            tempSeq.seqCycle++;
                        }
                    }
                    else{
                        break;
                    }
                }
            }
        }
    }

    // This is to clear out the rest of the acks
    while(outstandingPackets > 0){
        // Same as above
        fds[0].events = 0;
        fds[0].events |= POLLIN;
        if(poll(fds, 1, ackTimeout) != 0){
            if (recvfrom(receiveSocket, chunk, 64, 0, (struct sockaddr *)&serverReceive, &slen) == -1){
                cout << "Error in receiving";
                return -1;
            }

            packet *dataPacket = new packet(0, 0, 0, NULL);
            dataPacket->deserialize(chunk);
                lastAck = dataPacket->getSeqNum();
                clientAckLog << dataPacket->getSeqNum() << endl;

                sb.seqNum = (sb.seqNum + 1) % totalFrames;
                if (sb.seqNum == 0){
                    sb.seqCycle++;
                }

                outstandingPackets--;
                log(sb.seqNum, ns.seqNum, outstandingPackets, dataPacket);
        }

        else{
                seqTrack tempSeq = sb;
                while (tempSeq.seqCycle <= ns.seqCycle){
                    if(tempSeq.seqNum < ns.seqNum || tempSeq.seqCycle < ns.seqCycle){
                        memset((char *) &chunk, 0, sizeof(chunk));
                        packet *tempPacket = window[tempSeq.seqNum];
                        tempPacket->serialize(chunk);
                        if (sendto(sendSocket, chunk, 64, 0, (struct sockaddr *)&serverSend, slen)==-1){
                            cout << "Error in sendto function";
                            return -1;
                        }
                        log(sb.seqNum, ns.seqNum, outstandingPackets, tempPacket);
                        tempSeq.seqNum = (tempSeq.seqNum + 1) % totalFrames;
                        if(tempSeq.seqNum == 0){
                            tempSeq.seqCycle++;
                        }
                    }
                    else{
                        break;
                    }
                }
        }
    }

    // Sending client end of transmission
    memset((char *) &chunk, 0, sizeof(chunk));
    packet *clientEot = new packet(3, finalFrame+1, 0, NULL);
    clientEot->serialize(chunk);
    if (sendto(sendSocket, chunk, 64, 0, (struct sockaddr *)&serverSend, slen) == -1){
        cout << "Error in sending";
        return -1;
    }
  
    log(sb.seqNum, ns.seqNum, outstandingPackets, clientEot);
    clientSeqNumLog << clientEot->getSeqNum() << endl;

    memset((char *) &chunk2, 0, sizeof(chunk2));

    // receiving server end of transmission
    if (recvfrom(receiveSocket, chunk2, 64, 0, (struct sockaddr *)&serverReceive, &slen) == -1){
        cout << "Error in receiving";
        return -1;
    }


    packet *serverEot = new packet(2, clientEot->getSeqNum(), 0, NULL);
    serverEot->deserialize(chunk2);

    clientAckLog << serverEot->getSeqNum() << endl;
    log(sb.seqNum, ns.seqNum, outstandingPackets, serverEot);

    // closing up shop
    close(sendSocket);
    close(receiveSocket);
    in_file.close();
    clientAckLog.close();
    clientSeqNumLog.close();
}

void log(int sb, int ns, int outstandingPackets, packet *window);