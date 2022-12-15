#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>
#include <time.h>

#define BUFFER_SIZE 1500
#define nsec_per_sec 1000000000000L
#define nsec_in_usec 1000L
#define MAX_RTT 80000

struct ACK {
  int seq_nb, amount;
};

int sendPart(int data_socket_desc, char server_message[BUFFER_SIZE], char result[BUFFER_SIZE], int part, struct timespec* sentData, FILE * file, struct sockaddr_in data_addr) {
  fseek(file, (part*(BUFFER_SIZE-6)), SEEK_SET);
  size_t reading_size;
  reading_size = fread(server_message, 1, BUFFER_SIZE-6, file);
  sprintf(result, "%06d", part+1);
  for (int i=6; i<BUFFER_SIZE; i++) {
    result[i] = server_message[i-6];
  }

  struct timespec starttime;
  clock_gettime(CLOCK_REALTIME, &starttime);

  if (sendto(data_socket_desc, result, reading_size+6, 0, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
    fclose(file);
    perror("ERROR SENDING DATA\n");
    return -1;
    //retry
  }
  /*for(int i=0; i < BUFFER_SIZE; i++) {
    printf(" %2x", result[i]);
  }
  putchar('\n');*/
  printf("Sent part %d of %ld bytes\n", part+1, reading_size+6);
  sentData[part] = starttime;
  return 0;
}

struct ACK readAck(char client_message[BUFFER_SIZE], struct ACK* highest_ack_p) {
  char* seq = (char *)malloc(6*sizeof(char));
  for (int i=3; i<3+sizeof(seq); i++) {
    seq[i-3] = client_message[i];
  }
  int seq_nb = atoi(seq);
  free(seq);
  struct ACK ack;
  if (seq_nb > highest_ack_p->seq_nb) {
    highest_ack_p->seq_nb = seq_nb;
    highest_ack_p->amount = 1;
    printf("1 ACK %06d RECEIVED\n", seq_nb);
  } else if (seq_nb == highest_ack_p->seq_nb) {
    highest_ack_p->amount += 1;
    printf("%d ACK %06d RECEIVED\n", highest_ack_p->amount, seq_nb);
  }
  ack.seq_nb = seq_nb;
  ack.amount = 1;

  return ack;
}

int FlightSize(struct timespec* sentData, struct ACK *highest_ack_p) {
  int flightsize = 0;
  int i = highest_ack_p->seq_nb;
  while(1) {
    if (sentData[i].tv_nsec <= 0) {
      break;
    }
    i++;
    flightsize++;
  }
  return flightsize;
}

int calculatePartsToSend(int file_size) {
  int part = 0;
  while((part * (BUFFER_SIZE-6)) < file_size) {
    part++;
  }
  return part;
}

int main(int argc, char *argv[]){
  // https://stackoverflow.com/questions/60832185/how-to-send-any-file-image-exe-through-udp-in-c
    int port = 2000;
    int data_port = 4999;
    if (argc != 2) {
      perror("Please enter port as following: ./server <server_port>\n");
      return -1;
    }
    port = atoi(argv[1]);
    int socket_desc, data_socket_desc;
    struct sockaddr_in server_addr, data_addr;
    char server_message[BUFFER_SIZE], client_message[BUFFER_SIZE];
    
    // Create UDP socket:
    socket_desc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    if(socket_desc < 0){
        perror("Error while creating socket\n");
        return -1;
    }
    printf("Socket created successfully\n");
    
    // Set port and IP:
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    int server_struct_length = sizeof(server_addr);
    
    // Bind to the set port and IP:
    if(bind(socket_desc, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("Couldn't bind to the port\n");
        return -1;
    }
    printf("Done with binding\n");
    
    while (1) {
      // Clean buffers:
      memset(server_message, '\0', sizeof(server_message));
      memset(client_message, '\0', sizeof(client_message));
      printf("Listening for incoming connection...\n\n");
      
      // Receive client's message:
      if (recvfrom(socket_desc, client_message, sizeof(client_message), 0,
          (struct sockaddr*)&server_addr, &server_struct_length) < 0){
          perror("Couldn't receive\n");
          return -1;
      }
      printf("Received message from IP: %s and port: %i\n",
            inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));
      
      printf("Msg from client: %s\n", client_message);
      
      // Respond to client:
      if (strncmp(client_message, "SYN", 3) == 0) {
        data_port++;
        char * portStr = (char *)malloc(12*sizeof(char));
        sprintf(portStr, "SYN-ACK%d", data_port);
        strcpy(server_message, portStr);
        free(portStr);
        if (fork() == 0) {
          struct timespec transfer_start;
          clock_gettime(CLOCK_REALTIME, &transfer_start);
          struct timeval rtt;
          // Create UDP socket:
          data_socket_desc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
          if(data_socket_desc < 0){
              perror("Error while creating data socket\n");
              exit(1);
          }
          printf("Data socket created successfully\n");
          // Set port and IP:
          data_addr.sin_family = AF_INET;
          data_addr.sin_port = htons(data_port);
          data_addr.sin_addr.s_addr = htonl(INADDR_ANY);
          int data_struct_length = sizeof(data_addr);
      
          // Bind to the set port and IP:
          if(bind(data_socket_desc, (struct sockaddr*)&data_addr, sizeof(data_addr)) < 0){
              perror("Couldn't bind to the data port\n");
              exit(1);
          }
          printf("Done with binding data\n");
          if (sendto(socket_desc, server_message, strlen(server_message), 0,
            (struct sockaddr*)&server_addr, server_struct_length) < 0){
            perror("Can't send\n");
            return -1;
          }
          printf("Listening for incoming messages...\n\n");

          if (recvfrom(data_socket_desc, client_message, sizeof(client_message), 0, (struct sockaddr*)&data_addr, &data_struct_length) < 0) {
            perror("Error when trying to receive msg\n");
            return -1;
          }
          
          FILE * file;
          printf("File name to transfer: %s\n", client_message);
          file = fopen(client_message, "rb");
          if (file == NULL) {
            printf("Couldn't open the file.\n");
            close(data_socket_desc);
            exit(1);
          }
          fseek(file, 0, SEEK_END);
          int file_size = ftell(file);
          int part = 0;
          char result[BUFFER_SIZE];
          //result[6] = ' ';
          int cwnd = 30; //30 c'est pas trop mal
          fd_set desc_set;
          FD_ZERO(&desc_set);
          struct timeval nowait;
          struct ACK highest_ack;
          //int ssthresh = 999;
          struct timespec* sentData = (struct timespec*)malloc(999999*sizeof(struct timespec));
          int estimatedRtt = MAX_RTT;
          int oldRtt = estimatedRtt;
          int maxPart = calculatePartsToSend(file_size);
          while(highest_ack.seq_nb < maxPart) {
            struct timespec endtime;
            while(FlightSize(sentData, &highest_ack) < cwnd) {
              nowait.tv_usec = 0;
              nowait.tv_sec = 0;
              FD_SET(data_socket_desc, &desc_set);
              if (part >= highest_ack.seq_nb) {
                sendPart(data_socket_desc, server_message, result, part, sentData, file, data_addr);
              }
              part++;
              select(data_socket_desc+1, &desc_set, NULL, NULL, &nowait);
              if (FD_ISSET(data_socket_desc, &desc_set)) {
                if (recvfrom(data_socket_desc, client_message, sizeof(client_message), 0, (struct sockaddr*)&data_addr, &data_struct_length) < 0) {
                  perror("Error when trying to receive ACK msg\n");
                  return -1;
                }
                clock_gettime(CLOCK_REALTIME, &endtime);
                struct ACK ack = readAck(client_message, &highest_ack);
                int time_taken = ((endtime.tv_sec - sentData[ack.seq_nb-1].tv_sec)*nsec_per_sec + endtime.tv_nsec - sentData[ack.seq_nb-1].tv_nsec)/nsec_in_usec;
                estimatedRtt = (7.0/8.0)*oldRtt + (1.0/8.0)*time_taken;
                if (estimatedRtt > MAX_RTT) estimatedRtt = MAX_RTT;
                if (estimatedRtt < 0) estimatedRtt = 1;
                oldRtt = estimatedRtt;
                if (highest_ack.seq_nb) {
                  break;
                }
                //cwnd++;
                if (highest_ack.amount >= 4) {
                  part = highest_ack.seq_nb;
                  printf("Resending segment %d\n", highest_ack.seq_nb+1);
                  highest_ack.amount = 0;
                  //cwnd = 1;
                  //ssthresh = FlightSize(sentData, &highest_ack)/2;
                  sendPart(data_socket_desc, server_message, result, part, sentData, file, data_addr);
                }
              }
            }
            rtt.tv_sec = 0;
            rtt.tv_usec = estimatedRtt;
            printf("RTT=%d\n", estimatedRtt);
            FD_SET(data_socket_desc, &desc_set);
            select(data_socket_desc+1, &desc_set, NULL, NULL, &rtt);
            if (FD_ISSET(data_socket_desc, &desc_set)) {
              if (recvfrom(data_socket_desc, client_message, sizeof(client_message), 0, (struct sockaddr*)&data_addr, &data_struct_length) < 0) {
                perror("Error when trying to receive ACK msg\n");
                return -1;
              }
              //cwnd++;
              clock_gettime(CLOCK_REALTIME, &endtime);
              struct ACK ack = readAck(client_message, &highest_ack);
              int time_taken = ((endtime.tv_sec - sentData[ack.seq_nb-1].tv_sec)*nsec_per_sec + endtime.tv_nsec - sentData[ack.seq_nb-1].tv_nsec)/nsec_in_usec;
              estimatedRtt = (7.0/8.0)*oldRtt + (1.0/8.0)*time_taken;
              if (estimatedRtt > MAX_RTT) estimatedRtt = MAX_RTT;
              if (estimatedRtt < 0) estimatedRtt = 1;
              oldRtt = estimatedRtt;
              if (highest_ack.amount >= 4) {
                part = highest_ack.seq_nb;
                printf("Resending segment %d\n", highest_ack.seq_nb+1);
                highest_ack.amount = 0;
                //cwnd = 1;
                //ssthresh = FlightSize(sentData, &highest_ack)/2;
                sendPart(data_socket_desc, server_message, result, part, sentData, file, data_addr);
              }
            } else {
              printf("RTT Passed, retransmitting segment %d...\n", highest_ack.seq_nb+1);
              //cwnd = 1;
              //ssthresh = FlightSize(sentData, &highest_ack)/2;
              sendPart(data_socket_desc, server_message, result, highest_ack.seq_nb, sentData, file, data_addr);
            }
            printf("FlightSize=%d and cwnd=%d\n", FlightSize(sentData, &highest_ack), cwnd);
          }
          strcpy(server_message, "FIN");
          ssize_t end_result = sendto(data_socket_desc, server_message, sizeof(server_message), 0, (struct sockaddr*)&data_addr, data_struct_length);
          while (end_result < 0) {
            printf("Error while sending END pckt, retrying...\n");
            end_result = sendto(data_socket_desc, server_message, sizeof(server_message), 0, (struct sockaddr*)&data_addr, data_struct_length);
            sleep(1);
          }
          struct timespec transfer_end;
          clock_gettime(CLOCK_REALTIME, &transfer_end);
          float time_taken = ((transfer_end.tv_sec - transfer_start.tv_sec)*1000000000000.0f + transfer_end.tv_nsec - transfer_start.tv_nsec)/1000000000000.0f;
          printf("Time taken: %.3fs\n", time_taken);
          printf("Débit: %.0lf o/s\n", file_size/time_taken);
          close(data_socket_desc);
          fclose(file);
          exit(1);       
          return -1;
        }
      }
    }
    
    // Close the socket:
    close(socket_desc);
    
    return 0;
}
