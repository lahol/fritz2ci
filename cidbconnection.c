#include "cidbconnection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

/** @brief Send a message to the specified socket
 *  @param[in] sock The Socket
 *  @param[in] dbmsg The message.
 *  @return 0 on success.
 */
int cidbcon_send_message(int sock, CIDBMessage * dbmsg) {
  CIDBMsgTransmission tmsg;
  unsigned char buffer[CIDBMSG_MSG_TOTAL_SIZE];
  unsigned long int bytes;
  tmsg.completeMsg = malloc(sizeof(CIDBMsg));
  cidbmsg_write_message(dbmsg, tmsg.completeMsg);
  
  bytes = cidbmsg_transmission_start(buffer, &tmsg);
  while (bytes) {
    if (send(sock, buffer, bytes, 0) != (size_t)bytes) {
      return 1;
    }
    bytes = cidbmsg_transmission_continue(buffer, &tmsg);
  }
  return 0;
}

/** @brief Receive a message from the specified socket
 *  @param[in] sock The Socket
 *  @param[in] dbmsg The message.
 *  @return 0 on success.
 */
int cidbcon_recv_message(int sock, CIDBMessage * dbmsg) {
  unsigned char buffer[CIDBMSG_MSG_TOTAL_SIZE];
  unsigned long int bytes;
  unsigned long int offset;
  unsigned long int bytes_read = 0;
  CIDBMsg msg;
  CIDBMsgHeader header;
  bytes = (unsigned long int)recv(sock, buffer, CIDBMSG_MSG_TOTAL_SIZE, 0);
  if (bytes < CIDBMSG_HEADER_SIZE) {
    return 1;
  }
  offset = cidbmsg_read_header(&msg.header, buffer);
  if (msg.header.dataSize) {
    msg.data = malloc(msg.header.dataSize);
    memcpy(&msg.data[msg.header.offset], &buffer[offset], msg.header.partSize);
    bytes_read += msg.header.partSize;
  }    
  else {
    msg.data = NULL;
  }
  
  while (bytes_read < msg.header.dataSize) {
    bytes = (unsigned long int)recv(sock, buffer, CIDBMSG_MSG_TOTAL_SIZE, 0);
    if (bytes < CIDBMSG_HEADER_SIZE) {
      return 1;
    }
    memset(&header, 0, sizeof(CIDBMsgHeader));
    offset = cidbmsg_read_header(&header, buffer);
    memcpy(&msg.data[header.offset], &buffer[offset], header.partSize);
    bytes_read += header.partSize;
  }
  msg.header.partSize = bytes_read;
  msg.header.offset = 0;
  cidbmsg_read_message(&msg, dbmsg);
  free(msg.data);
  
  return 0;
}
