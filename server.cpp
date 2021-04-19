/*
    Author: Caleb Mills         studentID: 903566199        netID: ctm364
    Date: 04/12/2021
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

using namespace std;

// Log function to print out a log to the terminal
void log(const int expectedSeqNum, packet *dataPacket){
    if(dataPacket->getType() == 1){
        cout << "---------------------------------" << endl;
        dataPacket->printContents();
        cout << "expected Rn: " << expectedSeqNum << endl;
        cout << "sn: " << dataPacket->getSeqNum() << endl;
    }
    else{
        dataPacket->printContents();
    }
}

int main(int argc, char *argv[]){
    if(argc != 5){
        cout << "Usage: server <servername> <serverListenPort> <serverSendPort> <filename>" << endl;
    }

    char *receivePort = argv[2];
    char *sendPort = argv[3];

    struct hostent *s;
    s = gethostbyname(argv[1]);
    // Socket initialization
    struct sockaddr_in clientSend;
    struct sockaddr_in clientReceive;

    int sendSocket = 0;
    int receiveSocket = 0;
    socklen_t clen = sizeof(clientSend);
    char chunk[64];
  
    // Go-Back-N protocol variables
    int expectedSeqNum = 0;
    int lastAck = 0;
    int totalFrames = 8;
    int windowSize = 7;
    int outstandingPackets = 0;
    bool eot = false;

    // Opening the files for writing to
    ofstream arrivalLog("arrival.log", std::ios::out | std::ios::trunc);
    ofstream out_file(argv[4], std::ios::out | std::ios::trunc);

    // Send socket creation
    if ((sendSocket=socket(AF_INET, SOCK_DGRAM, 0))==-1) {
       cout << "Error in send socket creation.\n"; 
    }

    memset((char *) &clientSend, 0, sizeof(clientSend));
    clientSend.sin_family = AF_INET;
    clientSend.sin_port = htons(stoi(sendPort));
    bcopy((char *)s->h_addr, 
	(char *)&clientSend.sin_addr.s_addr,
	s->h_length);

    // Receive Socket Creation
    if ((receiveSocket=socket(AF_INET, SOCK_DGRAM, 0))==-1) {
       cout << "Error in receieve socket creation.\n"; 
    }

    memset((char *) &clientReceive, 0, sizeof(clientReceive));
    clientReceive.sin_family = AF_INET;
    clientReceive.sin_port = htons(stoi(receivePort));
    bcopy((char *)s->h_addr, 
	(char *)&clientReceive.sin_addr.s_addr,
	s->h_length);
    if (bind(receiveSocket, (struct sockaddr *)&clientReceive, sizeof(clientReceive)) == -1){
        cout << "Error in receive binding.\n";
    }

    while(1){
        // Initialize variables
        char chunk[64];
        char data[31];
            
        memset((char *) &data, 0, sizeof(data));
        memset((char *) &chunk, 0, sizeof(chunk));

        // receive from client
        if (recvfrom(receiveSocket, chunk, 64, 0, (struct sockaddr *)&clientReceive, &clen) == -1){
            cout << "Error in receiving packet" << endl;
        }

        // Get packet information
        packet *dataPacket = new packet(0, 0, 0, data);
        dataPacket->deserialize(chunk);

        // If the expected sequence number matches then we can move forward,
        // else discard the packet and timeout client
        if (expectedSeqNum == dataPacket->getSeqNum()){
            log(expectedSeqNum, static_cast<packet *>(dataPacket));
            arrivalLog << dataPacket->getSeqNum() << endl;
            out_file << dataPacket->getData() << endl;
            lastAck = dataPacket->getSeqNum();

            if (dataPacket->getType() == 3){
                break;
            }
            
            memset((char *) &chunk, 0, sizeof(chunk));
            packet *ackPacket = new packet(0, lastAck, 0, NULL);
            log(expectedSeqNum, ackPacket);
            ackPacket->serialize(chunk);

            if (sendto(sendSocket, chunk, 64, 0, (struct sockaddr *)&clientSend, clen) == -1){
                cout << "Error in sending packet" << endl;
            }

            expectedSeqNum = (expectedSeqNum + 1) % totalFrames;
        }
    }

    // Sending the server eot packet
    memset((char *) &chunk, 0, sizeof(chunk));
    packet *eotPacket = new packet(2, lastAck, 0, NULL);
    log(expectedSeqNum, eotPacket);
    eotPacket->serialize(chunk);
    if (sendto(sendSocket, chunk, 64, 0, (struct sockaddr *)&clientSend, clen) == -1){
        cout << "Error in sending packet" << endl;
    }

    // closing up shop
    close(sendSocket);
    close(receiveSocket);
    out_file.close();
    arrivalLog.close();
}