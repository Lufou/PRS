#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>

#define BUFFER_SIZE 1500
#define RTT 1
#define CREDIT 10

int sendPart(int data_socket_desc, char server_message[BUFFER_SIZE], char result[BUFFER_SIZE], int part, FILE * file, struct sockaddr_in data_addr) {
  fseek(file, (part*(BUFFER_SIZE-6)), SEEK_SET);
  size_t reading_size;
  reading_size = fread(server_message, 1, BUFFER_SIZE-6, file);
  for (int i=6; i<BUFFER_SIZE; i++) {
    result[i] = server_message[i-6];
  }

  if (sendto(data_socket_desc, result, reading_size+6, 0, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
    fclose(file);
    perror("ERROR SENDING DATA\n");
    return -1;
    //exit(1);
  }
  printf("Sent part %d of %ld bytes\n", part, reading_size+6);
  return 0;
}

int main(int argc, char *argv[]){
  // https://stackoverflow.com/questions/60832185/how-to-send-any-file-image-exe-through-udp-in-c
    int port = 2000;
    if (argc != 2) {
      perror("Please enter port as following: ./server <server_port>\n");
      return -1;
    }
    port = atoi(argv[1]);
    int socket_desc, data_socket_desc;
    struct sockaddr_in server_addr, data_addr;
    char server_message[BUFFER_SIZE], client_message[BUFFER_SIZE];
    
    // Clean buffers:
    memset(server_message, '\0', sizeof(server_message));
    memset(client_message, '\0', sizeof(client_message));
    
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
        strcpy(server_message, "SYN-ACK5000");
        if (fork() == 0) {
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
          data_addr.sin_port = htons(5000);
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
          
          //if (strcmp(client_message, "ACK") == 0) {
            FILE * file;
            printf("File name to transfer: %s\n", client_message);
            file = fopen(client_message, "rb");
            if (file == NULL) {
              printf("Couldn't open the file.\nPlease enter a valid file name: ");
              close(data_socket_desc);
              exit(1);
            }
            fseek(file, 0, SEEK_END);
            int file_size = ftell(file);
            int part = 0;
            char result[BUFFER_SIZE];
            sprintf(result, "%06d", part);
            //result[6] = ' ';
            int credit = CREDIT;
            fd_set desc_set;
            FD_ZERO(&desc_set);
            struct timeval nowait;
            while((part * (BUFFER_SIZE-6)) < file_size) {
              while(credit > 0) {
                nowait.tv_usec = 0;
                nowait.tv_sec = 0;
                FD_SET(data_socket_desc, &desc_set);
                sendPart(data_socket_desc, server_message, result, part, file, data_addr);
                credit--;
                select(data_socket_desc+1, &desc_set, NULL, NULL, &nowait);
                if (FD_ISSET(data_socket_desc, &desc_set)) {
                  if (recvfrom(data_socket_desc, client_message, sizeof(client_message), 0, (struct sockaddr*)&data_addr, &data_struct_length) < 0) {
                    perror("Error when trying to receive ACK msg\n");
                    return -1;
                  }
                  credit++;
                }
                part++;
                sprintf(result, "%06d", part);
              }
              rtt.tv_sec = RTT;
              FD_SET(data_socket_desc, &desc_set);
              select(data_socket_desc+1, &desc_set, NULL, NULL, &rtt);
              if (FD_ISSET(data_socket_desc, &desc_set)) {
                if (recvfrom(data_socket_desc, client_message, sizeof(client_message), 0, (struct sockaddr*)&data_addr, &data_struct_length) < 0) {
                  perror("Error when trying to receive ACK msg\n");
                  return -1;
                }
                credit++;
              } else {
                prinft("RTT Passed, retransmitting segment %d...\n", part);
                sendPart(data_socket_desc, server_message, result, part, file, data_addr);
              }
            }
            strcpy(server_message, "FIN");
            if (sendto(data_socket_desc, server_message, sizeof(server_message), 0, (struct sockaddr*)&data_addr, data_struct_length) < 0) {
              fclose(file);
              exit(1);
            }
          //}          
          return -1;
        }
      }
    }
    
    // Close the socket:
    close(socket_desc);
    
    return 0;
}
