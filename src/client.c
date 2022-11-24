#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUFFER_SIZE 2000
#define MAX_SEGMENTS 900

/*
TDL :
client : ne pas faire les traitements si on reçoit 2 fois le même ACK
serveur : faire mécanisme de traitement des acks
client : flush tables si on atteint la capa max des tableaux + write into file

*/

int main(void){
    int socket_desc, data_socket_desc;
    struct sockaddr_in server_addr, data_addr;
    char server_message[BUFFER_SIZE], client_message[BUFFER_SIZE];
    int data_struct_length;
    
    // Clean buffers:
    memset(server_message, '\0', sizeof(server_message));
    memset(client_message, '\0', sizeof(client_message));
    
    // Create socket:
    socket_desc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    if(socket_desc < 0){
        perror("Error while creating socket\n");
        return -1;
    }
    printf("Socket created successfully\n");
    
    // Set port and IP:
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(2000);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int server_struct_length = sizeof(server_addr);
    
    strcpy(client_message, "SYN");
    
    // Send the message to server:
    if(sendto(socket_desc, client_message, strlen(client_message), 0,
         (struct sockaddr*)&server_addr, server_struct_length) < 0){
        perror("Unable to send message\n");
        return -1;
    }
    
    // Receive the server's response:
    if(recvfrom(socket_desc, server_message, sizeof(server_message), 0,
         (struct sockaddr*)&server_addr, &server_struct_length) < 0){
        perror("Error while receiving server's msg\n");
        return -1;
    }
    
    printf("Server's response: %s\n", server_message);

    if (strncmp(server_message, "SYN-ACK", 7) == 0) {
      strcpy(client_message, "ACK");
      char* data_port_str = (char *)malloc(4*sizeof(char));
      strncpy(data_port_str, server_message+7, 4);
      data_socket_desc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if(data_socket_desc < 0){
          perror("Error while creating data socket\n");
          return -1;
      }
      printf("Data socket created successfully\n");
      // Set port and IP:
      data_addr.sin_family = AF_INET;
      data_addr.sin_port = htons(atoi(data_port_str));
      data_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
      data_struct_length = sizeof(data_addr);
      free(data_port_str);
      if(sendto(data_socket_desc, client_message, strlen(client_message), 0,
        (struct sockaddr*)&data_addr, data_struct_length) < 0){
        perror("Unable to send ACK message");
        return -1;
      }
    } else {
      printf("No SYN-ACK received, exit\n");
      return -1;
    }

    FILE* file;
    int received_size = 0;

    printf("Listening data socket...\n");
    if (recvfrom(data_socket_desc, server_message, sizeof(server_message), 0, (struct sockaddr *)&data_addr, &data_struct_length) < 0) {
      perror("Couldn't receive filename.\n");
      close(data_socket_desc);
      close(socket_desc);
      exit(1);
    }
    char *tmp = strdup(server_message);
    strcpy(server_message, "transfered_");
    strcat(server_message, tmp);
    free(tmp);
    file = fopen(server_message, "wb");
    int seq_received[MAX_SEGMENTS];
    char tab_segments[MAX_SEGMENTS][BUFFER_SIZE];
    int my_ack = 0;
    char ack_string[9];
    sprintf(ack_string, "ACK%06d", my_ack);
    while(1) {
      memset(server_message, '\0', sizeof(server_message));
      printf("Listening data socket...\n");
      received_size = recvfrom(data_socket_desc, server_message, sizeof(server_message), 0, (struct sockaddr *)&data_addr, &data_struct_length);
      if(received_size == SO_ERROR) {
          fclose(file);
          perror("Error while receiving server's msg\n");
          exit(1);
      }
      if (strcmp(server_message, "FIN") == 0) {
        printf("Transmission ended by the server.\n");
        //fwrite(tab_segments, sizeof(char), received_size-14, file);
        break;
      }     
      char seq[6];
      for (int i=0; i<sizeof(seq); i++) {
        seq[i] = server_message[i];
      }
      char* p = server_message + 6; 
      int seq_nb = atoi(seq);
      seq_received[seq_nb]++;
      strcpy(tab_segments[seq_nb], p);
      for(int i=0; i<MAX_SEGMENTS; i++) {
        if (seq_received[i] == 0) {
          my_ack = i-1;
          break;
        }
      }
      sprintf(ack_string, "ACK%06d", my_ack);
      if(sendto(data_socket_desc, ack_string, strlen(ack_string), 0,
        (struct sockaddr*)&data_addr, data_struct_length) < 0){
        perror("Unable to send ACK message");
        return -1;
      }
    }
    fclose(file);
    
    // Close the socket:
    close(data_socket_desc);
    close(socket_desc);
     
    return 0;
}
