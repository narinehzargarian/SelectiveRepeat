#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <fcntl.h>
#include <netdb.h> 


// =====================================

#define RTO 500000 /* timeout in microseconds */
#define HDR_SIZE 12 /* header size*/
#define PKT_SIZE 524 /* total packet size */
#define PAYLOAD_SIZE 512 /* PKT_SIZE - HDR_SIZE */
#define WND_SIZE 10 /* window size*/
#define MAX_SEQN 25601 /* number of sequence numbers [0-25600] */
#define FIN_WAIT 2 /* seconds to wait after receiving FIN*/

// Packet Structure
struct packet {
    unsigned short seqnum;
    unsigned short acknum;
    char syn;
    char fin;
    char ack;
    char dupack;
    unsigned int length;
    char payload[PAYLOAD_SIZE];
};

// Printing Functions
void printRecv(struct packet* pkt) {
    printf("RECV %d %d%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", (pkt->ack || pkt->dupack) ? " ACK": "");
}

void printSend(struct packet* pkt, int resend) {
    if (resend)
        printf("RESEND %d %d%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", pkt->ack ? " ACK": "");
    else
        printf("SEND %d %d%s%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", pkt->ack ? " ACK": "", pkt->dupack ? " DUP-ACK": "");
}

void printTimeout(struct packet* pkt) {
    printf("TIMEOUT %d\n", pkt->seqnum);
}

void printTimeoutLast(struct packet* pkt){
    printf("TIMEOUT %d\n", pkt->seqnum + pkt->length);
}


// Building a packet by filling the header and contents.
// This function is provided to you and you can use it directly
void buildPkt(struct packet* pkt, unsigned short seqnum, unsigned short acknum, char syn, char fin, char ack, char dupack, unsigned int length, const char* payload) {
    pkt->seqnum = seqnum;
    pkt->acknum = acknum;
    pkt->syn = syn;
    pkt->fin = fin;
    pkt->ack = ack;
    pkt->dupack = dupack;
    pkt->length = length;
    memcpy(pkt->payload, payload, length);
}

// =====================================

double setTimer() {
    struct timeval e;
    gettimeofday(&e, NULL);
    return (double) e.tv_sec + (double) e.tv_usec/1000000 + (double) RTO/1000000;
}

double setFinTimer() {
    struct timeval e;
    gettimeofday(&e, NULL);
    return (double) e.tv_sec + (double) e.tv_usec/1000000 + (double) FIN_WAIT;
}

int isTimeout(double end) {
    struct timeval s;
    gettimeofday(&s, NULL);
    double start = (double) s.tv_sec + (double) s.tv_usec/1000000;
    return ((end - start) < 0.0);
}
// Other helpers ===========================================================
bool allWindowReceived(struct packet* pkts, int bound = WND_SIZE){
    for(int i = 0; i < bound; i++){
        if(!pkts[i].ack)
            return false;
    }
    return true;
}


int getPktIndex(struct packet* pkts, int ack_received, int s, int e){
    
    for(int i = 0; i < WND_SIZE; i++){
        if((pkts[i].seqnum + pkts[i].length) % MAX_SEQN == ack_received && !pkts[i].ack){
            return i;
        }
    }
   
    return -1;
    
}

// =====================================

int main (int argc, char *argv[])
{
    if (argc != 5) {
        perror("ERROR: incorrect number of arguments\n "
               "Please use \"./client <HOSTNAME-OR-IP> <PORT> <ISN> <FILENAME>\"\n");
        exit(1);
    }

    struct in_addr servIP;
    if (inet_aton(argv[1], &servIP) == 0) {
        struct hostent* host_entry; 
        host_entry = gethostbyname(argv[1]); 
        if (host_entry == NULL) {
            perror("ERROR: IP address not in standard dot notation\n");
            exit(1);
        }
        servIP = *((struct in_addr*) host_entry->h_addr_list[0]);
    }

    unsigned int servPort = atoi(argv[2]);
    unsigned short initialSeqNum = atoi(argv[3]);

    FILE* fp = fopen(argv[4], "r");
    if (fp == NULL) {
        perror("ERROR: File not found\n");
        exit(1);
    }

    // =====================================
    // Socket Setup

    int sockfd;
    struct sockaddr_in servaddr;
    
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr = servIP;
    servaddr.sin_port = htons(servPort);
    memset(servaddr.sin_zero, '\0', sizeof(servaddr.sin_zero));

    int servaddrlen = sizeof(servaddr);
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    // =====================================
    // Establish Connection
    struct packet synpkt, synackpkt;
    unsigned short seqNum = initialSeqNum;

    buildPkt(&synpkt, seqNum, 0, 1, 0, 0, 0, 0, NULL);

    printSend(&synpkt, 0);
    sendto(sockfd, &synpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    double timer = setTimer();
    int n;

    while (1) {
        while (1) {
            n = recvfrom(sockfd, &synackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);

            if (n > 0)
                break;
            else if (isTimeout(timer)) {
                printTimeout(&synpkt);
                printSend(&synpkt, 1);
                sendto(sockfd, &synpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                timer = setTimer();
            }
        }

        printRecv(&synackpkt);
        if ((synackpkt.ack || synackpkt.dupack) && synackpkt.syn && synackpkt.acknum == (seqNum + 1) % MAX_SEQN) {
            seqNum = synackpkt.acknum;
            break;
        }
    }

    // =====================================
    // FILE READING VARIABLES
    
    char buf[PAYLOAD_SIZE];
    size_t m;

    // =====================================
    // CIRCULAR BUFFER VARIABLES

    struct packet ackpkt;
    struct packet pkts[WND_SIZE];
    double pkts_timer[WND_SIZE];

    int s = 0;
    int e = 0;
    int full = 0;
    int ack_expected = 0; 
    int eof = 0;
    int inWindow = false;
    int next_to_send = 0;
    int isFirst = 1;
    long numAcked = 0;
    long numSent = 0;
    long numResent = 0;
    bool finish = false;

    // =====================================
    // Send First Packet (ACK containing payload)

    m = fread(buf, 1, PAYLOAD_SIZE, fp);

    buildPkt(&pkts[e], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, m, buf);
    printSend(&pkts[e], 0);
    sendto(sockfd, &pkts[e], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    pkts_timer[e] = setTimer();
    buildPkt(&pkts[e], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 0, 1, m, buf);

    
    e = 1;
    while (1) {
        //send all the packets up to the window size
        while(!full && !eof){
            if((e + 1) % WND_SIZE == s)
                full = 1;

            m = fread(buf, 1, PAYLOAD_SIZE, fp);
            if(m == 0){
                eof = 1;
                e = (e - 1 + WND_SIZE) % WND_SIZE;
                break;
            }
            int prev_packet = ((e-1) + WND_SIZE) % WND_SIZE;

            if(isFirst){
                isFirst = 0;
                pkts[prev_packet].ack = 0;
            }

            buildPkt(&pkts[e], (pkts[prev_packet].seqnum + pkts[prev_packet].length) % MAX_SEQN, 0, 0, 0, 0, 0, m, buf);
            printSend(&pkts[e], 0);
            sendto(sockfd, &pkts[e], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
            pkts_timer[e] = setTimer();
            
            numSent++;
            e = (e + 1) % WND_SIZE;

            if(eof)
                break; //Wait for ACKs
        }
        while(1){ //Wait for ACKs
            n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);
            if (n > 0) {
                printRecv(&ackpkt);
                int new_ind = getPktIndex(pkts, ackpkt.acknum, s, e);
                if(new_ind != -1){
                    pkts[new_ind].ack = 1;
                    numAcked++;
                }

                int j = 0;
                while(pkts[s].ack && j < WND_SIZE){
                    
                    full = 0;
                    s = (s + 1) % WND_SIZE;
                    j++;
                }
                break;
            }
            else{
                if(s <= e){
                    if(s==e){
                        if(isTimeout(pkts_timer[s]) && !pkts[s].ack){
                            printTimeout(&pkts[s]);
                            printSend(&pkts[s],1);
                            sendto(sockfd, &pkts[s], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                            pkts_timer[s] = setTimer();
                        }
                         else{
                            
                            for(int i= (s + 1) % WND_SIZE; i != (e + 1) % WND_SIZE; i = (i + 1) % WND_SIZE){
                                if(isTimeout(pkts_timer[i]) && !pkts[i].ack){
                                    numResent++;
                                    printTimeout(&pkts[i]);
                                    printSend(&pkts[i],1);
                                    sendto(sockfd, &pkts[i], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                                    pkts_timer[i] = setTimer();
                                }
                            }
                        }
                        
                    }

                    else{
                        for(int i = s; i <= e; i++){
                                if(isTimeout(pkts_timer[i]) && !pkts[i].ack){
                                    numResent++;
                                    printTimeout(&pkts[i]);
                                    printSend(&pkts[i],1);
                                    sendto(sockfd, &pkts[i], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                                    pkts_timer[i] = setTimer();
                                }
                            }
                    }
                }
                else{
                    for(int i = s; i < WND_SIZE; i++){
                        if(isTimeout(pkts_timer[i]) && !pkts[i].ack){
                            numResent++;
                            printTimeout(&pkts[i]);
                            printSend(&pkts[i],1);
                            sendto(sockfd, &pkts[i], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                            pkts_timer[i] = setTimer();
                        }
                    }

                    for(int i = 0; i <= e; i++){
                        if(isTimeout(pkts_timer[i]) && !pkts[i].ack){
                            printTimeout(&pkts[i]);
                            printSend(&pkts[i],1);
                            sendto(sockfd, &pkts[i], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                            pkts_timer[i] = setTimer();
                        }
                    }
                }
                
            }     

        }
        if(eof && pkts[e].ack){
            if(numSent < WND_SIZE && allWindowReceived(pkts, numSent)){
                //printf("numsent is %d\n", numSent);
                break;
            }
            else if(allWindowReceived(pkts))
                break;

        } 

    }

    // *** End of your client implementation ***
    fclose(fp);

    // =====================================
    // Connection Teardown
    struct packet finpkt, recvpkt;
    buildPkt(&finpkt, ackpkt.acknum, 0, 0, 1, 0, 0, 0, NULL);
    buildPkt(&ackpkt, (ackpkt.acknum + 1) % MAX_SEQN, (ackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, 0, NULL);

    printSend(&finpkt, 0);
    sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    timer = setTimer();
    int timerOn = 1;

    double finTimer;
    int finTimerOn = 0;

    while (1) {
        while (1) {
            n = recvfrom(sockfd, &recvpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);

            if (n > 0)
                break;
            if (timerOn && isTimeout(timer)) {
                printTimeout(&finpkt);
                printSend(&finpkt, 1);
                if (finTimerOn)
                    timerOn = 0;
                else
                    sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                timer = setTimer();
            }
            if (finTimerOn && isTimeout(finTimer)) {
                close(sockfd);
                if (! timerOn)
                    exit(0);
            }
        }
        printRecv(&recvpkt);
        if ((recvpkt.ack || recvpkt.dupack) && recvpkt.acknum == (finpkt.seqnum + 1) % MAX_SEQN) {
            timerOn = 0;
        }
        else if (recvpkt.fin && (recvpkt.seqnum + 1) % MAX_SEQN == ackpkt.acknum) {
            printSend(&ackpkt, 0);
            sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
            finTimer = setFinTimer();
            finTimerOn = 1;
            buildPkt(&ackpkt, ackpkt.seqnum, ackpkt.acknum, 0, 0, 0, 1, 0, NULL);
        }
    }
}
