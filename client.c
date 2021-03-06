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

// Packet Structure: Described in Section 2.1.1 of the spec. DO NOT CHANGE!
struct packet {
    unsigned short seqnum; // start bit of packet
    unsigned short acknum; // expected seqnum from next packet 
    char syn; // 1 if it is a connection request 
    char fin; // 1 if it is a connection close request
    char ack; // 1 if it is an ack
    char dupack; // 1 if it is an duplicate ack
    unsigned int length;
    char payload[PAYLOAD_SIZE];
};



// Printing Functions: Call them on receiving/sending/packet timeout according
// Section 2.6 of the spec. The content is already conformant with the spec,
// no need to change. Only call them at correct times.
void printRecv(struct packet* pkt) {
    //printf("RECV %d %d%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", (pkt->ack || pkt->dupack) ? " ACK": "");
    printf("RECV %d %d%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", (pkt->ack) ? " ACK": "",(pkt->dupack) ? " DUPACK": "");

}

void printSend(struct packet* pkt, int resend);
void printSend(struct packet* pkt, int resend) {
    if (resend)
        printf("RESEND %d %d%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", pkt->ack ? " ACK": "");
    else
        printf("SEND %d %d%s%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", pkt->ack ? " ACK": "", pkt->dupack ? " DUP-ACK": "");
}

void printTimeout(struct packet* pkt) {
    printf("TIMEOUT %d\n", pkt->seqnum);
}
int isCumulativeAck(int *s, int *e, struct packet *pkts, struct packet *recvpkt, int *newS); 


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

/*
    s = first not-yet-acked packet
    e = last not-yet-acked packet
*/
int isFull(int s, int e, int *packetCount){ // returns 1 if full, 0 if empty, 2 if neither
    if (*packetCount == 10){ 
        return 1; 
    }else if (*packetCount == 0){ 
        return 0; 
    }else{ 
        return 2; 
    }
}

int isCumulativeAck(int *s, int *e, struct packet *pkts, struct packet *recvpkt, int *newS){ 
        if(*e > *s){
            for(int i = *s; i< *e; i+=1){
                if (recvpkt->acknum == ((pkts[i].seqnum + pkts[i].length) % MAX_SEQN )){ 
                    *newS = (i + 1) % 10; 
                    return 1; 
                }
            }
        }
        else if (*e < *s || *e == *s){ 
            for (int i = *s; i < 10; i += 1){ 
                if (recvpkt->acknum ==( (pkts[i].seqnum + pkts[i].length) % MAX_SEQN) ){ 
                    *newS = (i + 1) % 10; 
                    return 1; 
                }

            }
            for (int i = 0; i < *e; i += 1){ 
                 if (recvpkt->acknum == ((pkts[i].seqnum + pkts[i].length) % MAX_SEQN) ){ 
                    *newS = (i + 1) % 10; 
                    return 1; 
                }
            }

        }
        return 0; 
}


/*/*                                                  !!! pass in a pointer !!!*/
int receiveAcks(int *s, int *e, int *packetCount, struct packet *pkts, int sockfd, struct packet *recvpkt, struct sockaddr_in servaddr, int servaddrlen, int *currAckNum, int *timerOnData, double *timer){
    int n = recvfrom(sockfd, recvpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);
    printRecv(recvpkt);
    // if (n > 0 && recvpkt->dupack == 1){ 
    //         printRecv(recvpkt); 
    //         //printf("RECV DUPACK \n");
    // }
    if(n > 0 && recvpkt->ack == 1 ){
        //printf("%d", *s);
       // printf("%s", ": "); 
        int oldestSeqNum = pkts[*s].seqnum ; 
        //printf("%d\n", oldestSeqNum); 
        //for testing purposes

        if (recvpkt->acknum == ((oldestSeqNum+ pkts[*s].length) % MAX_SEQN)){ 
            //printf("IN ORDER ACK \n");
            
            //printf("oldestseqnum: "); 
            //printf("%d \n", oldestSeqNum); 

            //pkts[*s] = NULL;
            *packetCount -= 1; 
            *s = (*s+1)%10;
            *currAckNum = recvpkt->seqnum + 1; 
           //oldestSeqNum = (oldestSeqNum+ pkts[*s].length) % MAX_SEQN; 

            //if(isFull != 0){
            *timer = setTimer(); 
            *timerOnData = 1; 
            //}
            //else { 
             //   *timerOnData = 0; 
            //}
            return 1; // received an ack, move the window
        }
        

        int newS = -1; 
        if (isCumulativeAck(s, e, pkts, recvpkt, &newS) == 1 ){ 
            //printf("OUT OF ORDER ACK \n");
            //printf("oldestseqnum: "); 
            //printf("%d \n", oldestSeqNum); 
            //int increment = (recvpkt->acknum - oldestSeqNum) / 512; 
            int increment = -1; 
            if (newS > *s){
                increment = newS - *s; 
            }else {
                increment = (10 - *s) + newS; 
            }
            // if ((recvpkt->acknum - oldestSeqNum) - (512 * increment ) > 0 ){ 
            //     increment += 1; 
            // }
            //printf("increment: ");
            //printf("%d \n", increment);            
            *packetCount -= increment; 
            *s = (*s+increment)%10;
            //printf("restart timer on %d\n", pkts[*s].seqnum);
            *timer = setTimer();
            *timerOnData = 1;  
            // need to restart the timer here
        }

    }
    return 0; // receives an ack, but it is ignored
}

void resendWindow(int *s, int*e, struct packet *pkts, int *packetCount, int *timerOnData, double *timer, int sockfd, struct sockaddr_in servaddr, int servaddrlen, struct packet *recvpkt, int *currAckNum){ // upon timeout, resennd every packet in the window
    //printf("RESEND WINDOW"); 
    //sendto(sockfd, &pkts[*s], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    //printf("Window[s] = " ); 
    //printf("s: %d, e: %d\n", *s, *e);
    if(*e > *s){
        for(int i = *s; i< *e; i+=1){
            sendto(sockfd, &pkts[i], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
            printSend(&pkts[i], 1);
            if (i == *s){ 
                *timer = setTimer();
                *timerOnData = 1;  
            }
            receiveAcks(s, e, packetCount, pkts, sockfd, recvpkt, servaddr, servaddrlen, currAckNum, timerOnData, timer);

        }
    }
    else if (*e < *s || *e == *s){ 
        for (int i = *s; i < 10; i += 1){ 
            sendto(sockfd, &pkts[i], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
            printSend(&pkts[i], 1);
            if (i == *s){ 
                *timer = setTimer(); 
                *timerOnData = 1; 
            }
            receiveAcks(s, e, packetCount, pkts, sockfd, recvpkt, servaddr, servaddrlen, currAckNum, timerOnData, timer);

        }
        for (int i = 0; i < *e; i += 1){ 
            sendto(sockfd, &pkts[i], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
            printSend(&pkts[i], 1);
            receiveAcks(s, e, packetCount, pkts, sockfd, recvpkt, servaddr, servaddrlen, currAckNum, timerOnData, timer);


        }
    }

    
}

/*
    case 1: pkts is full (of not-yet acked packets), fp is not EOF
*/
void case1(char buf[PAYLOAD_SIZE], int *nextSeqNum, int *s, int *e, int *packetCount, struct packet *pkts, int sockfd, FILE* fp,  struct packet *recvpkt, struct sockaddr_in servaddr, int servaddrlen, int *currAckNum, int *timerOnData, double *timer);

void case2(char buf[PAYLOAD_SIZE], int *nextSeqNum, int *s, int *e, int *packetCount, struct packet *pkts, int sockfd, FILE* fp,  struct packet *recvpkt, struct sockaddr_in servaddr, int servaddrlen, int *currAckNum, int *timerOnData, double *timer);
void resetBuf(char *buf);

void case1(char buf[PAYLOAD_SIZE], int *nextSeqNum, int *s, int *e, int *packetCount, struct packet *pkts , int sockfd, FILE* fp,  struct packet *recvpkt, struct sockaddr_in servaddr, int servaddrlen, int *currAckNum, int *timerOnData, double *timer)
{
    /* assume we checked pkts is full and not EOF already */
    //int n;
    int received = 0;
    //if pkts is full and EOF not reached
    if(isFull(*s, *e, packetCount)== 1 ) {   
        while(1){ // loop until receive an ack
                 if (*timerOnData == 1 &&  isTimeout(*timer)){
                    //printf("\ntimeout 1\n");
                    printTimeout(&pkts[*s]); 
                    
                    //(int *s, int*e, struct packet *pkts, int *packetCount, int *timerOnData, double *timer, int sockfd, struct sockaddr_in servaddr, int servaddrlen, struct packet recvpkt, int *currAckNum)
                    resendWindow(s, e, pkts, packetCount, timerOnData, timer, sockfd, servaddr, servaddrlen, recvpkt, currAckNum);
                }
            if (receiveAcks(s, e, packetCount, pkts, sockfd, recvpkt, servaddr, servaddrlen, currAckNum, timerOnData, timer) == 1){ 
                break; 
            }   
        }
        /*
            i did not check for EOF because no file is read
        */
    }
    if (isFull(*s,*e, packetCount) == 2){ 
        receiveAcks(s, e, packetCount, pkts, sockfd, recvpkt, servaddr, servaddrlen, currAckNum, timerOnData, timer); 
        case2(buf, nextSeqNum,  s, e, packetCount,pkts, sockfd, fp, recvpkt, servaddr, servaddrlen, currAckNum, timerOnData, timer);
    }
}

/*
    case 2: pkts is not full, 
*/
  void case2(char buf[PAYLOAD_SIZE], int *nextSeqNum, int *s, int *e, int *packetCount, struct packet *pkts, int sockfd, FILE* fp,  struct packet *recvpkt, struct sockaddr_in servaddr, int servaddrlen, int *currAckNum, int *timerOnData, double *timer){
        while(isFull(*s, *e, packetCount) != 1 && !feof(fp)){ 

            if (*timerOnData==1 && isTimeout(*timer)){
                //do something
                //("timeout 2\n");
                printTimeout(&pkts[*s]); 
                resendWindow(s, e, pkts, packetCount, timerOnData, timer, sockfd, servaddr, servaddrlen, recvpkt, currAckNum);
            }
            receiveAcks(s, e, packetCount, pkts, sockfd, recvpkt, servaddr, servaddrlen, currAckNum, timerOnData, timer); 
            int bytes = fread(buf, 1, PAYLOAD_SIZE, fp);
            //if (bytes == PAYLOAD_SIZE){ 
            buildPkt(&pkts[*e], *nextSeqNum , 0, 0, 0, 0, 0, bytes, buf);
            printSend(&pkts[*e], 0);
            resetBuf(buf); 
            sendto(sockfd, &pkts[*e], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
            if (*timerOnData == 0){ 
                *timer = setTimer(); 
                *timerOnData = 1; 
            }
            *packetCount += 1; 
            *nextSeqNum += pkts[*e].length; 
            *nextSeqNum = *nextSeqNum % MAX_SEQN; 
            *e = (*e + 1) % 10; 
            //}  
            }
        if (isFull(*s, *e, packetCount) == 1 && !feof(fp)){ 
            case1(buf, nextSeqNum,  s, e, packetCount, pkts, sockfd, fp, recvpkt, servaddr, servaddrlen, currAckNum, timerOnData, timer );
        }

        if(feof(fp)){ //!full and !empty and end of file
            //clear window 
            //printf("\n end of file\n");
            while(isFull(*s, *e, packetCount) != 0 ){ //loop until window empty
                if (isTimeout(*timer)){
                    //printf("timeout 3\n");
                    printTimeout(&pkts[*s]); 
                    resendWindow(s, e, pkts, packetCount, timerOnData, timer, sockfd, servaddr, servaddrlen, recvpkt, currAckNum);
                }
                receiveAcks(s, e, packetCount, pkts, sockfd, recvpkt, servaddr, servaddrlen, currAckNum, timerOnData, timer);
         }
        if(isFull(*s, *e, packetCount) == 0){ 
                *timerOnData = 0;
        }
        
        }            
    }

    void resetBuf(char *buf){ 
        for(int i = 0; i < PAYLOAD_SIZE; i++){ 
            buf[i] = '0'; 
        }
    }
// =====================================

int main (int argc, char *argv[])
{
    if (argc != 4) {
        perror("ERROR: incorrect number of arguments\n");
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

    FILE* fp = fopen(argv[3], "r");
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

    // NOTE: We set the socket as non-blocking so that we can poll it until
    //       timeout instead of getting stuck. This way is not particularly
    //       efficient in real programs but considered acceptable in this
    //       project.
    //       Optionally, you could also consider adding a timeout to the socket
    //       using setsockopt with SO_RCVTIMEO instead.
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    // =====================================
    // Establish Connection: This procedure is provided to you directly and is
    // already working.
    // Note: The third step (ACK) in three way handshake is sent along with the
    // first piece of along file data thus is further below

    struct packet synpkt, synackpkt;

    unsigned short seqNum = rand() % MAX_SEQN;

    buildPkt(&synpkt, seqNum, 0, 1, 0, 0, 0, 0, NULL);

    printSend(&synpkt, 0);
    /* send TCP SYN */
    sendto(sockfd, &synpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    double timer = setTimer();
    int n;

    while (1) {
        while (1) {
            /* receive SYNACK */
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
    int s = 0; //oldest unacked packet 
    int e = 0; //most recent unacked packet + 1
    int packetCount = 0; //when packetCount == 10, buffer full
    int full = 0;
    int baseNum = 0;  

    // =====================================
    // Send First Packet (ACK containing payload)

    m = fread(buf, 1, PAYLOAD_SIZE, fp);
    //buildPkt(struct packet* pkt, unsigned short seqnum, unsigned short acknum, char syn, char fin, char ack, char dupack, unsigned int length, const char* payload)

                        /*  seqNum,  ackNum,                     syn = 0, fin = 0, ack = 1, dupack = 0 */
    buildPkt(&pkts[0], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, m, buf);
    e+= 1; 
    printSend(&pkts[0], 0);
    /* send ACK containing payload */
    sendto(sockfd, &pkts[0], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    packetCount += 1; 

    timer = setTimer(); // reset timer upon sending ACK with payload
    int timerOnData = 1; 
    /* after sending the pkt[0], set dupack = 1 in case pkt[0] is to be resent  */
                                            /* syn = 0, fin = 0, ack = 0, dupack = 1 */
    buildPkt(&pkts[0], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 0, 1, m, buf);


    // =====================================
    // *** TODO: Implement the rest of reliable transfer in the client ***
    // Implement GBN for basic requirement or Selective Repeat to receive bonus

    // Note: the following code is not the complete logic. It only sends a
    //       single data packet, and then tears down the connection without
    //       handling data loss.
    //       Only for demo purpose. DO NOT USE IT in your final submission

    //  seqnum = bits that is expected from the next msg

    /* 
        while (1) {
   //     /* receive ACK of first packet */
    // n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);
    //    if (n > 0) {
     //       break;
    //  -1  0  1  2  3  4  5  6  7  8  9
    //      s                            
    //                                 e


 //*
    int nextseqnum = (seqNum + pkts[0].length) % MAX_SEQN; 
    int currAckNum = 0; 
    case2(buf, &nextseqnum, &s, &e, &packetCount, pkts, sockfd, fp, &ackpkt, servaddr, servaddrlen, &currAckNum,  &timerOnData, &timer);   
    fclose(fp);

    // =====================================
    // Connection Teardown: This procedure is provided to you directly and is
    // already working.

    struct packet finpkt, recvpkt;
    buildPkt(&finpkt, nextseqnum, 0, 0, 1, 0, 0, 0, NULL);
    buildPkt(&ackpkt, (nextseqnum + 1) % MAX_SEQN, currAckNum, 0, 0, 1, 0, 0, NULL);

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


/*       
       void case2(char buf[PAYLOAD_SIZE]) {

           while(nextseqnum >= baseNum + N && !feof(fp)){ 
            int a = recvfrom(sockfd, buf, BUFSIZE, MSG_DONTWAIT,
            (struct sockaddr * ) & clientaddr, & clientlen);
            if (a > 0 && getBit(buf, ACK)) {
               slideWindow() //base += 1 
               }
            index = nextseqnum % N ; 
            int bytes = fread(buf, 1, PAYLOAD_SIZE, fp);
            if (bytes == 512){ 
                buildPkt(&pkts[index], nextseqnum, ackNum, 0, 0, 0, 0, m, buf);
            }


            send(&pkts[index]); 
            nextseqnum += PACKET_SIZE; 



           }
       }

        void resetBuf(char *buf){ 
            for(int i = 0; i < sizeof(buf); i++){ 
                buf[i] = '0'; 
        }
    }
<<<<<<< HEAD
*/// ===== send 10 packets ========

        /* 
        case 1: pkts is full, fp is not EOF 
            if(pkts is full and not EOF): 
                chekc for acks 
                if(receive ack):
                    base = getacknum(rcvpkt)+1
                    kick the packet out of the window
                    go back to while(pkts is not filled) 
                                if(receive ack):
                    base = getacknum(rcvpkt)+1
                    kick the packet out of the window s 
            if(EOF): 
                break 
            if (pkts is not full and not EOF)
                case2()

        func case 2: pkts is not full 
             while(pkts is not filled AND not EOF):    
                check ack  = if receive ack, kick out the packet, base++
                read file and buildPkt, send packet, push it(not-yet-acked) into pkts
                check ack  = if receive ack, kick out the packet, base++
                            
                //nextseqnum = first unsent packet in pkts
                //base = first not-yet-acked in pkts

            if (EOF): 
                break 
            if(pkts is filled and not EOF): 
                case1() 

        terminate connection 

        //things to figure out:    
            //keep track of nextSeqNum and Base 
            //keep track of whether payload is full, else pad with 0s
            //how to check for acks? (check recvFrom ack flag )

        */  
        /*

        
           


            if(EOF):
                break
            


        while(data left in message and buffer not full)
            if msg > 512 bytes
                read 512 bytes, put into packet 
            else 
                read msg_len bytes and pad with 0s to 512
            
            put packet into client buffer (if there's space in window)
                if window full of unacked pkts, wait until window gets ack 


            from buffer/window: 
                while (unsent packets in buffer = nextseqnum < base + N )
                    send oldest unsent packets to server ( sendto() )
                        keep packet in buffer (as unacked/sent pkt) until acked 
                            if window receives ack (assume it is the oldest unacked)- remove acked packet from window, open window to accept unsent pkt 
                    nextseqnum ++ 

        
        
    // ===== finish break up files into packets ========/* 
    int bytes = fp.size() ; 
    int n = nextSeqNum ; 
    int ackNum = (synackpkt.seqnum + 1) % MAX_SEQN
    
    while(fp != EOF; and pn < WND_SIZE) : 
        seqNum ++ 1; = 1
        ackNum 
        n += 1; 
        if(bytes >= 512){
;            m = fread(buf, 1, PAYLOaD_SIZE, fp);
        buildPkt(&pkts[n], seqNum, ackNum, 0, 0, 0, 0, m, buf);
        
            bytes -= 512; 
        } 
        else{ 
            m = fread(buf, 1, bytes, fp)
            //pad with 0s till 512 
            bytes = 0;  
        }

        //reset n, 
        //account for sent, unacked packets 



        ackNum 
        n += 1; 
        if(bytes >= 512){
;            m = fread(buf, 1, PAYLOaD_SIZE, fp);
        buildPkt(&pkts[n], seqNum, ackNum, 0, 0, 0, 0, m, buf);
        
            bytes -= 512; 
        } 
        else{ 
            m = fread(buf, 1, bytes, fp)
            //pad with 0s till 512 
            bytes = 0;  
        }

        //reset n, 
        //account for sent, unacked packets 

*/
