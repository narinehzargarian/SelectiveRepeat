#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>

// =====================================

#define RTO 500000 /* timeout in microseconds */
#define HDR_SIZE 12 /* header size*/
#define PKT_SIZE 524 /* total packet size */
#define PAYLOAD_SIZE 512 /* PKT_SIZE - HDR_SIZE */
#define WND_SIZE 10 /* window size*/
#define MAX_SEQN 25601 /* number of sequence numbers [0-25600] */
#define INVALID_SEQ_NUM ((uint16_t)-1)
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

int isTimeout(double end) {
    struct timeval s;
    gettimeofday(&s, NULL);
    double start = (double) s.tv_sec + (double) s.tv_usec/1000000;
    return ((end - start) < 0.0);
}

// Helpers =====================================================
        int compare(const void* a, const void* b){
            struct packet pkt1 = *((const struct packet*)a);
            struct packet pkt2 = *((const struct packet*)b);

            if(pkt1.seqnum < pkt2.seqnum)
                return -1;
            else if(pkt1.seqnum > pkt2.seqnum)
                return 1;
            else
                return 0;
        }
    

// =====================================

int main (int argc, char *argv[])
{
    if (argc != 3) {
        perror("ERROR: incorrect number of arguments\n"
               "Please use command \"./server <PORT> <ISN>\"\n");
        exit(1);
    }

    unsigned int servPort = atoi(argv[1]);
    unsigned short initialSeqNum = atoi(argv[2]);

    // =====================================
    // Socket Setup

    int sockfd;
    struct sockaddr_in servaddr, cliaddr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(servPort);
    memset(servaddr.sin_zero, '\0', sizeof(servaddr.sin_zero));

    if (bind(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == -1) {
        perror("bind() error");
        exit(1);
    }

    int cliaddrlen = sizeof(cliaddr);

    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    // =====================================


    for (int i = 1; ; i++) {
        // =====================================
        // Establish Connection
        unsigned short seqNum = initialSeqNum;

        int n;

        FILE* fp;

        struct packet synpkt, synackpkt, ackpkt;

        while (1) {
            n = recvfrom(sockfd, &synpkt, PKT_SIZE, 0, (struct sockaddr *) &cliaddr, (socklen_t *) &cliaddrlen);
            if (n > 0) {
                printRecv(&synpkt);
                if (synpkt.syn)
                    break;
            }
        }

        unsigned short cliSeqNum = (synpkt.seqnum + 1) % MAX_SEQN; // next message from client should have this sequence number

        buildPkt(&synackpkt, seqNum, cliSeqNum, 1, 0, 1, 0, 0, NULL);

        while (1) {
            printSend(&synackpkt, 0);
            sendto(sockfd, &synackpkt, PKT_SIZE, 0, (struct sockaddr*) &cliaddr, cliaddrlen);
            
            while(1) {
                n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *) &cliaddr, (socklen_t *) &cliaddrlen);
                if (n > 0) {
                    printRecv(&ackpkt);
                    if (ackpkt.seqnum == cliSeqNum && ackpkt.ack && ackpkt.acknum == (synackpkt.seqnum + 1) % MAX_SEQN) {

                        int length = snprintf(NULL, 0, "%d", i) + 6;
                        char* filename = (char*)malloc(length);
                        snprintf(filename, length, "%d.file", i);

                        fp = fopen(filename, "w");
                        free(filename);
                        if (fp == NULL) {
                            perror("ERROR: File could not be created\n");
                            exit(1);
                        }

                        fwrite(ackpkt.payload, 1, ackpkt.length, fp);

                        seqNum = ackpkt.acknum;
                        cliSeqNum = (ackpkt.seqnum + ackpkt.length) % MAX_SEQN;

                        buildPkt(&ackpkt, seqNum, cliSeqNum, 0, 0, 1, 0, 0, NULL);
                        printSend(&ackpkt, 0);
                        sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr*) &cliaddr, cliaddrlen);

                        break;
                    }
                    else if (ackpkt.syn) {
                        buildPkt(&synackpkt, seqNum, (synpkt.seqnum + 1) % MAX_SEQN, 1, 0, 0, 1, 0, NULL);
                        break;
                    }
                }
            }

            if (! ackpkt.syn)
                break;
        }
       
        struct packet recvpkt;
        struct packet buffer[WND_SIZE];
        for(int i = 0; i < WND_SIZE; i++){
            buffer[i].seqnum = INVALID_SEQ_NUM;
            buffer[i].ack = 1; 
        }
        int buffer_size = 0;
        int next_expected = cliSeqNum;
        int prev_expected = next_expected;
        int up_bound_seqnum, low_bound_seqnum;

        while(1) {
            n = recvfrom(sockfd, &recvpkt, PKT_SIZE, 0, (struct sockaddr *) &cliaddr, (socklen_t *) &cliaddrlen);
            up_bound_seqnum = next_expected + ((WND_SIZE - 1) * PAYLOAD_SIZE);
            low_bound_seqnum = next_expected - (WND_SIZE * PAYLOAD_SIZE);
            
            if (n > 0) {
                printRecv(&recvpkt);
                
                if (recvpkt.fin) {
                    cliSeqNum = (recvpkt.seqnum + 1) % MAX_SEQN;
                    buildPkt(&ackpkt, seqNum, cliSeqNum , 0, 0, 1, 0, 0, NULL);
                    printSend(&ackpkt, 0);
                    sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr*) &cliaddr, cliaddrlen);

                    break;
                }
                else if(up_bound_seqnum <= MAX_SEQN && recvpkt.seqnum >= next_expected && recvpkt.seqnum <= up_bound_seqnum){
                    int ind = (recvpkt.seqnum/ PAYLOAD_SIZE) % WND_SIZE;
                    buffer[ind] = recvpkt;
                    int ack_num = (recvpkt.seqnum + recvpkt.length) % MAX_SEQN;
                    buildPkt(&ackpkt, ackpkt.seqnum, ack_num, 0, 0, 1, 0, 0, NULL);
                    printSend(&ackpkt, 0);
                    sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr*) &cliaddr, cliaddrlen);
                }

                else if(up_bound_seqnum > MAX_SEQN && (recvpkt.seqnum >= next_expected || recvpkt.seqnum <= up_bound_seqnum % MAX_SEQN)){
                    int ind = (recvpkt.seqnum/ PAYLOAD_SIZE) % WND_SIZE;
                    buffer[ind] = recvpkt;
                    int ack_num = (recvpkt.seqnum + recvpkt.length) % MAX_SEQN;
                    buildPkt(&ackpkt, ackpkt.seqnum, ack_num, 0, 0, 1, 0, 0, NULL);
                    printSend(&ackpkt, 0);
                    sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr*) &cliaddr, cliaddrlen);
                }
                
                
                else if(low_bound_seqnum >= 0 && recvpkt.seqnum < next_expected){ //Retransmission for lost ACK
                    int ack_num = (recvpkt.seqnum + recvpkt.length) % MAX_SEQN;
                    buildPkt(&ackpkt, ackpkt.seqnum, ack_num, 0, 0, 1, 0, 0, NULL);
                    printSend(&ackpkt, 0);
                    sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr*) &cliaddr, cliaddrlen);
                }
                else if(low_bound_seqnum < 0 && 
                    (recvpkt.seqnum < next_expected || recvpkt.seqnum < MAX_SEQN)){
                        //Lost ACK
                        int dup_ack = (recvpkt.seqnum + recvpkt.length) % MAX_SEQN;
                        buildPkt(&ackpkt, ackpkt.seqnum, dup_ack, 0, 0, 1, 0, 0, NULL);
                        printSend(&ackpkt, 0);
                        sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr*) &cliaddr, cliaddrlen);
                }
                while(!buffer[(next_expected/PAYLOAD_SIZE) % WND_SIZE].ack){
                    int ind = (next_expected/PAYLOAD_SIZE) % WND_SIZE;
                    fwrite(buffer[ind].payload, 1, buffer[ind].length, fp);
                    buffer[ind].ack = 1;
                    buffer_size++;
                    next_expected = (next_expected + PAYLOAD_SIZE) % MAX_SEQN;
                }
            }
            
        }

        fclose(fp);
        // =====================================
        // Connection Teardown
        struct packet finpkt, lastackpkt;
        buildPkt(&finpkt, seqNum, 0, 0, 1, 0, 0, 0, NULL);
        buildPkt(&ackpkt, seqNum, cliSeqNum, 0, 0, 0, 1, 0, NULL);

        printSend(&finpkt, 0);
        sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr*) &cliaddr, cliaddrlen);
        double timer = setTimer();

        while (1) {
            while (1) {
                n = recvfrom(sockfd, &lastackpkt, PKT_SIZE, 0, (struct sockaddr *) &cliaddr, (socklen_t *) &cliaddrlen);
                if (n > 0)
                    break;

                if (isTimeout(timer)) {
                    printTimeout(&finpkt);
                    printSend(&finpkt, 1);
                    sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr*) &cliaddr, cliaddrlen);
                    timer = setTimer();
                }
            }

            printRecv(&lastackpkt);
            if (lastackpkt.fin) {

                printSend(&ackpkt, 0);
                sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr*) &cliaddr, cliaddrlen);

                printSend(&finpkt, 1);
                sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr*) &cliaddr, cliaddrlen);
                timer = setTimer();
                
                continue;
            }
            if ((lastackpkt.ack || lastackpkt.dupack) && lastackpkt.acknum == (finpkt.seqnum + 1) % MAX_SEQN)
                break;
        }

        seqNum = lastackpkt.acknum;
    }
}
