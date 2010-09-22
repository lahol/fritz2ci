#include "cidbmessages.h"
#include <arpa/inet.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned short int _cidbmsg_strlen(unsigned char * msg, unsigned long int * bytes);
unsigned long int _cidbmsg_set_strlen(unsigned char * msg, unsigned short int len);
unsigned char * _cidbmsg_read_str(unsigned char * msg, unsigned short int len);

/** @brief Prepares the header for a network message
 *  @param[out] dst Buffer where the message ist to be stored
 *  @param[in] header The header to be translated
 *  @return The number of bytes written.
 */
unsigned long int cidbmsg_prepare_header(unsigned char * dst, CIDBMsgHeader * header) {
  if (!dst) return 0;
  if (!header) {
    memset(dst, 0, CIDBMSG_HEADER_SIZE);
    return CIDBMSG_HEADER_SIZE;
  }
  CIDBMSG_HEADER_SET_CLIENTID      (dst, header->ClientId);
  CIDBMSG_HEADER_SET_MSGCOMMAND    (dst, header->msgCommand);
  CIDBMSG_HEADER_SET_MSGSUBCOMMAND (dst, header->msgSubcommand);
  CIDBMSG_HEADER_SET_MSGID         (dst, header->msgId);
  CIDBMSG_HEADER_SET_MSGFLAGS      (dst, header->msgFlags);
  CIDBMSG_HEADER_SET_DATASIZE      (dst, header->dataSize);
  CIDBMSG_HEADER_SET_PARTSIZE      (dst, header->partSize);
  CIDBMSG_HEADER_SET_OFFSET        (dst, header->offset);
  return CIDBMSG_HEADER_SIZE;
}

/** @brief Reads the header from a stream into a structure
 *  @param[out] header The location of the header
 *  @param[in] src The buffer to read the header from
 *  @return The number of bytes read.
 */
unsigned long int cidbmsg_read_header(CIDBMsgHeader * header, unsigned char * src) {
  if (!header) return 0;
  if (!src) {
    memset(header, 0, CIDBMSG_HEADER_SIZE);
    return CIDBMSG_HEADER_SIZE;
  }
  header->ClientId      = CIDBMSG_HEADER_GET_CLIENTID(src);
  header->msgCommand    = CIDBMSG_HEADER_GET_MSGCOMMAND(src);
  header->msgSubcommand = CIDBMSG_HEADER_GET_MSGSUBCOMMAND(src);
  header->msgId         = CIDBMSG_HEADER_GET_MSGID(src);
  header->msgFlags      = CIDBMSG_HEADER_GET_MSGFLAGS(src);
  header->dataSize      = CIDBMSG_HEADER_GET_DATASIZE(src);
  header->partSize      = CIDBMSG_HEADER_GET_PARTSIZE(src);
  header->offset        = CIDBMSG_HEADER_GET_OFFSET(src);
  return CIDBMSG_HEADER_SIZE;
}

/** @brief Initialize the message header with the message specific information
 *  @param[in] clientid The client identifier as given by the register message
 *  @param[in] command The message command.
 *  @param[in] subcommand The subcommand of the message
 *  @param[in] msgid The identifier of the message.
 *  @param[out] header The header structure where the information is written to
 */
void cidbmsg_mkmessage(unsigned short int clientid,
                                     unsigned char command, 
                                     unsigned char subcommand, 
                                     unsigned long int msgid,
                                     CIDBMsgHeader * header) {
  header->ClientId = clientid;
  header->msgCommand = command;
  header->msgSubcommand = subcommand;
  header->msgId = msgid;
  header->msgFlags = 0;
  header->dataSize = 0;
  header->partSize = 0;
  header->offset = 0;
}

/** @brief Reads a encoded table from the buffer to a CIDBTable structure
 *  @param[in] msg The buffer holding the encoded table
 *  @param[out] table A pointer to a CIDBTable structure where the information will
 *                    be written to.
 *  @return The number of bytes read from the buffer.
 */
unsigned long int cidbmsg_read_table(unsigned char * msg, CIDBTable * table) {
  unsigned short int i, j;
  unsigned short int len;
  unsigned short int rowidx;
  unsigned long int bytes = 0;
  unsigned long int offset;
  if (!msg || !table) {
    return 0;
  }
  memset(table, 0, sizeof(CIDBTable));

  /* read the dimension of the table */
  table->nrows = CIDB_GET_USHORT(msg, 0);
  table->ncols = CIDB_GET_USHORT(msg, 2);
  if (table->ncols) {
    table->column_names = malloc(sizeof(unsigned char*)*table->ncols);
    if (table->nrows) {
      table->fields = malloc(sizeof(unsigned char*)*table->nrows*table->ncols);
    }
  }
  bytes += 4;
  /* read the column names */
  for (j = 0; j < table->ncols; j++) {
    len = _cidbmsg_strlen(&msg[bytes], &offset);
    bytes += offset;
    table->column_names[j] = _cidbmsg_read_str(&msg[bytes], len);
    bytes += len;
  }
  /* read the fields */
  for (i = 0; i < table->nrows; i++) {
    rowidx = i*table->ncols;
    for (j = 0; j < table->ncols; j++) {
      len = _cidbmsg_strlen(&msg[bytes], &offset);
      bytes += offset;
      table->fields[rowidx+j] = _cidbmsg_read_str(&msg[bytes], len);
      bytes += len;
    }
  }
  return bytes;
}

/** @brief Determine the required size of the table in a buffer
 *  @param[in] table The table to determine the size of
 *  @return The number of bytes required.
 */
unsigned long int cidbmsg_table_get_size(CIDBTable * table) {
  if (!table) {
    return 0;
  }
  unsigned short int len;
  unsigned short int i, j, rowidx;
  unsigned long int bytes = 4; /*for row, col */
  for (j = 0; j < table->ncols; j++) {
    len = strlen((char*)table->column_names[j]);
    len >= 255 ? bytes += 3 : bytes++;
    bytes += len;
  }
  for (i = 0; i < table->nrows; i++) {
    rowidx = i*table->ncols;
    for (j = 0; j < table->ncols; j++) {
      len = table->fields[rowidx+j] ? strlen((char*)table->fields[rowidx+j]) : 0;
      len >= 255 ? bytes += 3 : bytes++;
      bytes += len;
    }
  }
  return bytes;
}

/** @brief Writes a CIDBTable structure to a buffer
 *  @param[out] msg A pointer to the buffer where the table should be written to.
 *  @param[in] table The table to write to the buffer.
 *  @return The number of bytes written. This should match exactly the return value
 *          of \ref cidbmsg_table_get_size.
 */
unsigned long int cidbmsg_write_table(unsigned char * msg, CIDBTable * table) {
  unsigned short int len;
  unsigned short int i, j, rowidx;
  unsigned long int bytes = 4;
  
  if (!msg) return 0;
  if (!table) {
    memset(msg, 0, 4);
    return 4;
  }
  CIDB_SET_USHORT(msg, 0, table->nrows);
  CIDB_SET_USHORT(msg, 2, table->ncols);
  for (j = 0; j < table->ncols; j++) {
    len = table->column_names[j] ? strlen((char*)table->column_names[j]) : 0;
    bytes += _cidbmsg_set_strlen(&msg[bytes], len);
    if (table->column_names[j]) {
      memcpy(&msg[bytes], table->column_names[j], len);
    }
    bytes += len;
  }
  for (i = 0; i < table->nrows; i++) {
    rowidx = i*table->ncols;
    for (j = 0; j < table->ncols; j++) {
      len = table->fields[rowidx+j] ? strlen((char*)table->fields[rowidx+j]) : 0;
      bytes += _cidbmsg_set_strlen(&msg[bytes], len);
      if (table->fields[rowidx+j]) {
        memcpy(&msg[bytes], table->fields[rowidx+j], len);
      }
      bytes += len;
    }
  }
  return bytes;
}

/** @brief Write a string (containing lenght information) to a message buffer.
 *  @param[out] msg Pointer to the message buffer to write to.
 *  @param[in] str A Null-terminated string to write to the message buffer.
 *  @return The number of bytes written.
 */ 
unsigned long int cidbmsg_write_string(unsigned char * msg, unsigned char * str) {
  unsigned long int bytes = 0;
  unsigned short int len;
  unsigned short int i;
  len = str ? strlen((char*)str) : 0;
  bytes += _cidbmsg_set_strlen(msg, len);
  for (i = 0; i < len; i++, bytes++) {
    msg[bytes] = str[i];
  }
  return bytes;
}

/** @brief Read a string from a message buffer.
 *  @param[in] msg Pointer to the message buffer to read the data from.
 *  @param[out] str Pointer to the location where the new string should be stored.
 *  @return The number of bytes read.
 */
unsigned long int cidbmsg_read_string(unsigned char * msg, unsigned char ** str) {
  unsigned short int len;
  unsigned short int i;
  unsigned long int bytes;
  if (!msg) {
    return 0;
  }
  len = _cidbmsg_strlen(msg, &bytes);
  if (str) {
    (*str) = malloc(len+1);
    for (i = 0; i < len; i++, bytes++) {
      (*str)[i] = msg[bytes];
    }
    (*str)[len] = '\0';
    return bytes;
  }
  else {
    return bytes+len;
  }
}

/** @brief Begin the transmission of a message (prepare the partial message)
 *  @param[out] part The message buffer where the partial message should be stored in.
 *  @param[in] msg Pointer to a message transmission structure.
 *  @return The number of bytes written.
 */
unsigned long int cidbmsg_transmission_start(unsigned char * part, CIDBMsgTransmission * msg) {
  msg->completeMsg->header.partSize = CIDBMSG_DATA_SIZE < msg->completeMsg->header.dataSize ?
                                                     CIDBMSG_DATA_SIZE : msg->completeMsg->header.dataSize;
  msg->completeMsg->header.offset = 0;
/*  memcpy(part, &msg->completeMsg->header, CIDBMSG_HEADER_SIZE);*/ /* caused error due to aligning */ 
  cidbmsg_prepare_header(part, &msg->completeMsg->header);
  memcpy(&part[CIDBMSG_HEADER_SIZE], msg->completeMsg->data, msg->completeMsg->header.partSize);
  msg->offset = msg->completeMsg->header.partSize;
  return msg->completeMsg->header.partSize+CIDBMSG_HEADER_SIZE;
}

/** @brief Continue the the transmission of a message (prepare the partial message)
 *  @param[out] part The message buffer where the partial message should be stored in.
 *  @param[in] msg Pointer to a message transmission structure.
 *  @return The number of bytes written.
 */
unsigned long int cidbmsg_transmission_continue(unsigned char * part, CIDBMsgTransmission * msg) {
  if (msg->completeMsg->header.dataSize <= msg->offset) {
    return 0;
  }
  msg->completeMsg->header.partSize = CIDBMSG_DATA_SIZE <= msg->completeMsg->header.dataSize - msg->offset ?
                                      CIDBMSG_DATA_SIZE :  msg->completeMsg->header.dataSize - msg->offset;
  msg->completeMsg->header.offset = msg->offset;
/*  memcpy(part, &msg->completeMsg->header, CIDBMSG_HEADER_SIZE);*/
  cidbmsg_prepare_header(part, &msg->completeMsg->header); /* cause error due to aligning */
  memcpy(&part[CIDBMSG_HEADER_SIZE], &msg->completeMsg->data[msg->offset], msg->completeMsg->header.partSize);
  msg->offset += msg->completeMsg->header.partSize;
  return msg->completeMsg->header.partSize+CIDBMSG_HEADER_SIZE;
}

/** @internal
 *  @brief Determine the length of a message string.
 *  @param[in] msg Pointer to the start of the string.
 *  @param[out] bytes The number of bytes required to store the length information.
 *  @return The length of the string, excluding the length information.
 */
unsigned short int _cidbmsg_strlen(unsigned char * msg, unsigned long int * bytes) {
  unsigned short int len = CIDB_GET_UCHAR(msg, 0);
  (*bytes) = 1;
  if (len == 255) {
    len = CIDB_GET_USHORT(msg, 1);
    (*bytes) = 3;
  }
  return len;
}

/** @internal
 *  @brief Set the length of a message string.
 *  @param[out] msg Pointer to the location of the new string.
 *  @param[in] len Length of the string.
 *  @return The number of bytes used to store the length information.
 */
unsigned long int _cidbmsg_set_strlen(unsigned char * msg, unsigned short int len) {
  if (len >= 255) {
    CIDB_SET_UCHAR(msg, 0, 0xff);
    CIDB_SET_USHORT(msg, 1, len);
    return 3;
  }
  else {
    CIDB_SET_UCHAR(msg, 0, len);
    return 1;
  }
}

/** @internal
 *  @brief Read a string from a message buffer
 *  @param[in] msg Pointer to the message buffer (start of the string, not the length)
 *  @param[in] len The length of the string to read.
 *  @return Pointer to a newly allocated string.
 */
unsigned char * _cidbmsg_read_str(unsigned char * msg, unsigned short int len) {
  unsigned short int k;
  unsigned char * buffer = malloc(sizeof(unsigned char)*(len+1));
  if (!buffer) {
    return NULL;
  }
  for (k = 0; k < len; k++) {
    buffer[k] = msg[k];
  }
  buffer[len] = '\0';
  return buffer;
}

/** @internal
 *  @brief Write a message to a buffer.
 *  @param[in] dbmsg The easy to read message structure
 *  @param[out] msg The message buffer information.
 *  @return The size of the data part.
 */
unsigned long int cidbmsg_write_message(CIDBMessage * dbmsg, CIDBMsg* msg) {
  if (!dbmsg || !msg) {
    return 0;
  }
  unsigned long int size = 0/*CIDBMSG_HEADER_SIZE*/;
  unsigned short int len;
  unsigned short i;
  unsigned long int pos;

  memset(&msg->header, 0, sizeof(CIDBMSG_HEADER_SIZE));
  msg->header.msgCommand = dbmsg->cmd;
  msg->header.msgSubcommand = dbmsg->subcmd;
  msg->header.ClientId = dbmsg->clientid;
  msg->header.msgId = dbmsg->msgid;
  
  /* calculate the required space */
  switch (dbmsg->cmd) {
    case CIDBMSG_CMD_REGISTER:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
      }
      else if (dbmsg->subcmd == CIDBMSG_SUBCMD_RESP) {
        size += 2*sizeof(unsigned short int);
      }
      else {
        return 0;
      }
      break;
    case CIDBMSG_CMD_CALLER_LIST:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        size += sizeof(unsigned short int);
        size += 1;
        if (dbmsg->stringlist && dbmsg->stringlist[0]) {
          len = strlen((char*)dbmsg->stringlist[0]);
          if (len >= 255) size += 2;
          size += len;
        }
      }
      else if (dbmsg->subcmd == CIDBMSG_SUBCMD_RESP) {
        size += sizeof(unsigned short int);
        size += cidbmsg_table_get_size(&dbmsg->table);
      }
      else {
        return 0;
      }
      break;
    case CIDBMSG_CMD_READ_CALLER:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        size += 2*sizeof(unsigned short int);
        size += 1;
        if (dbmsg->stringlist && dbmsg->stringlist[0]) {
          len = strlen((char*)dbmsg->stringlist[0]);
          if (len >= 255) size += 2;
          size += len;
        }
      }
      else if (dbmsg->subcmd == CIDBMSG_SUBCMD_RESP) {
        size += sizeof(unsigned short int);
        size += cidbmsg_table_get_size(&dbmsg->table);
      }
      else {
        return 0;
      }
      break;
    case CIDBMSG_CMD_WRITE_CALLER:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        size += 2*sizeof(unsigned short int);
        size += cidbmsg_table_get_size(&dbmsg->table);
      }
      else if (dbmsg->subcmd == CIDBMSG_SUBCMD_RESP) {
        size += sizeof(unsigned short int);
        size += sizeof(unsigned long int);
      }
      else {
        return 0;
      }
      break;
    case CIDBMSG_CMD_WRITE_CALL:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        size += cidbmsg_table_get_size(&dbmsg->table);
      }
      else if (dbmsg->subcmd == CIDBMSG_SUBCMD_RESP) {
        size += sizeof(unsigned short int);
        size += sizeof(unsigned long int);
      }
      else {
        return 0;
      }
      break;
    case CIDBMSG_CMD_CALL_LIST:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        size += sizeof(unsigned long int);
        size += sizeof(unsigned short int);
      }
      else if (dbmsg->subcmd == CIDBMSG_SUBCMD_RESP) {
        size += sizeof(unsigned short int);
        size += cidbmsg_table_get_size(&dbmsg->table);
      }
      else {
        return 0;
      }
      break;
    case CIDBMSG_CMD_SHOWTABLES:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
      }
      else if (dbmsg->subcmd == CIDBMSG_SUBCMD_RESP) {
        size += 2*sizeof(unsigned short int);
        if (dbmsg->stringlist) {
          for (i = 0; i < dbmsg->nstrings; i++) {
            size += 1;
            if (dbmsg->stringlist[i]) {
              len = strlen((char*)dbmsg->stringlist[i]);
              if (len >= 255) size += 2;
              size += len;
            }
          }
        }
      }
      else {
        return 0;
      }
      break;
    case CIDBMSG_CMD_DELETE_CALLER:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        size += 6;
        if (dbmsg->stringlist) {
          if (dbmsg->stringlist[0]) {
            len = strlen((char*)dbmsg->stringlist[0]);
            if (len >= 255) size += 2;
            size += len;
          }
          if (dbmsg->stringlist[1]) {
            len = strlen((char*)dbmsg->stringlist[1]);
            if (len >= 255) size += 2;
            size += len;
          }
        }
      }
      else if (dbmsg->subcmd == CIDBMSG_SUBCMD_RESP) {
        size += 2;
      }
      else {
        return 0;
      }
      break;
    case CIDBMSG_CMD_LIST_INFO:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
      }
      else if (dbmsg->subcmd == CIDBMSG_SUBCMD_RESP) {
        size += 6;
      }
      else {
        return 0;
      }
      break;
    case CIDBMSG_CMD_DESCRIBE_TABLE:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        size += 1;
        if (dbmsg->stringlist && dbmsg->stringlist[0]) {
          len = strlen((char*)dbmsg->stringlist[0]);
          if (len >= 255) {
            size += 2;
          }
          size += len;
        }
      }
      else if (dbmsg->subcmd == CIDBMSG_SUBCMD_RESP) {
        size += 2*sizeof(unsigned short int);
        if (dbmsg->stringlist) {
          for (i = 0; i < dbmsg->nstrings; i++) {
            size += 1;
            if (dbmsg->stringlist[i]) {
              len = strlen((char*)dbmsg->stringlist[i]);
              if (len >= 255) size += 2;
              size += len;
            }
          }
        }        
      }
      else {
        return 0;
      }
      break;
    case CIDBMSG_CMD_LISTEN:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        size += sizeof(unsigned long int);
      }
      else {
        size += sizeof(unsigned short int);
      }
      break; 
    default: return 0;
  }
  
  /* set header information */
  memset(&msg->header, 0, sizeof(CIDBMSG_HEADER_SIZE));
  msg->header.msgCommand = dbmsg->cmd;
  msg->header.msgSubcommand = dbmsg->subcmd;
  msg->header.ClientId = dbmsg->clientid;
  msg->header.msgId = dbmsg->msgid;
  msg->header.dataSize = size;
  
  /* allocate memory */
  if (msg->header.dataSize) {
    msg->data = malloc(size);
  }
  else {
    msg->data = NULL;
    return 0;
  }
  
  /* write data */
  switch (dbmsg->cmd) {
    case CIDBMSG_CMD_REGISTER:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
      }
      else {
        CIDB_SET_USHORT(msg->data, 0, dbmsg->errcode);
        CIDB_SET_USHORT(msg->data, 2, dbmsg->clientid);
      }
      break; 
    case CIDBMSG_CMD_CALLER_LIST:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        CIDB_SET_USHORT(msg->data, 0, dbmsg->userid);
        if (dbmsg->stringlist && dbmsg->stringlist[0]) {
          cidbmsg_write_string(&msg->data[2], dbmsg->stringlist[0]);
        }
        else {
          CIDB_SET_UCHAR(msg->data, 2, 0);
        }
      }
      else {
        CIDB_SET_USHORT(msg->data, 0, dbmsg->errcode);
        cidbmsg_write_table(&msg->data[2], &dbmsg->table);
      }
      break; 
    case CIDBMSG_CMD_READ_CALLER:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        CIDB_SET_USHORT(msg->data, 0, dbmsg->userid);
        CIDB_SET_USHORT(msg->data, 2, (unsigned short int)dbmsg->flags);
        if (dbmsg->stringlist && dbmsg->stringlist[0]) {
          cidbmsg_write_string(&msg->data[4], dbmsg->stringlist[0]);
        }
        else {
          CIDB_SET_UCHAR(msg->data, 4, 0);
        }
      }
      else {
        CIDB_SET_USHORT(msg->data, 0, dbmsg->errcode);
        cidbmsg_write_table(&msg->data[2], &dbmsg->table);
      }
      break; 
    case CIDBMSG_CMD_WRITE_CALLER:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        CIDB_SET_USHORT(msg->data, 0, dbmsg->userid);
        CIDB_SET_USHORT(msg->data, 2, (unsigned short int)dbmsg->flags);
        cidbmsg_write_table(&msg->data[4], &dbmsg->table);
      }
      else {
        CIDB_SET_USHORT(msg->data, 0, dbmsg->errcode);
        CIDB_SET_ULONG(msg->data, 2, dbmsg->index);
      }
      break; 
    case CIDBMSG_CMD_WRITE_CALL:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        cidbmsg_write_table(msg->data, &dbmsg->table);
      }
      else {
        CIDB_SET_USHORT(msg->data, 0, dbmsg->errcode);
        CIDB_SET_ULONG(msg->data, 2, dbmsg->index);
      }
      break; 
    case CIDBMSG_CMD_CALL_LIST:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        CIDB_SET_ULONG(msg->data, 0, dbmsg->offset);
        CIDB_SET_USHORT(msg->data, 4, dbmsg->count);
      }
      else {
        CIDB_SET_USHORT(msg->data, 0, dbmsg->errcode);
        cidbmsg_write_table(&msg->data[2], &dbmsg->table);
      }
      break; 
    case CIDBMSG_CMD_SHOWTABLES:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
      }
      else {
        CIDB_SET_USHORT(msg->data, 0, dbmsg->errcode);
        if (dbmsg->stringlist) {
          CIDB_SET_USHORT(msg->data, 2, dbmsg->nstrings);
          pos = 4;
          for (i = 0; i < dbmsg->nstrings; i++) {
            pos += cidbmsg_write_string(&msg->data[pos], dbmsg->stringlist[i]);
          }
        }
        else {
          CIDB_SET_USHORT(msg->data, 2, 0);
        }
      }
      break; 
    case CIDBMSG_CMD_DESCRIBE_TABLE:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        if (dbmsg->stringlist) {
          cidbmsg_write_string(msg->data, dbmsg->stringlist[i]);
        }
        else {
          CIDB_SET_USHORT(msg->data, 0, 0);
        }
      }
      else {
        CIDB_SET_USHORT(msg->data, 0, dbmsg->errcode);
        if (dbmsg->stringlist) {
          CIDB_SET_USHORT(msg->data, 2, dbmsg->nstrings);
          pos = 4;
          for (i = 0; i < dbmsg->nstrings; i++) {
            pos += cidbmsg_write_string(&msg->data[pos], dbmsg->stringlist[i]);
          }
        }
        else {
          CIDB_SET_USHORT(msg->data, 2, 0);
        }
      }
      break; 
    case CIDBMSG_CMD_DELETE_CALLER:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        CIDB_SET_USHORT(msg->data, 0, dbmsg->userid);
        CIDB_SET_USHORT(msg->data, 2, dbmsg->nstrings);
        pos = 4;
        for (i = 0; i < dbmsg->nstrings; i++) {
          pos += cidbmsg_write_string(&msg->data[pos], dbmsg->stringlist[i]);
        }
      }
      else {
        CIDB_SET_USHORT(msg->data, 0, dbmsg->errcode);
      }
      break;
    case CIDBMSG_CMD_LIST_INFO:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
      }
      else {
        CIDB_SET_USHORT(msg->data, 0, dbmsg->errcode);
        CIDB_SET_ULONG(msg->data, 2, dbmsg->maxentries);
      }
      break;
    case CIDBMSG_CMD_LISTEN:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        CIDB_SET_ULONG(msg->data, 0, dbmsg->mask);
      }
      else {
        CIDB_SET_USHORT(msg->data, 0, dbmsg->errcode);
      }
      break; 
  }
  return size;
}

/** @brief Read a message from a message buffer to an easy to read structure.
 *  @param[in] msg Pointer to a message buffer information.
 *  @param[out] dbmsg Pointer to an easy to read message structure.
 */
void cidbmsg_read_message(CIDBMsg * msg, CIDBMessage * dbmsg) {
  unsigned long int pos;
  unsigned short int i;
  if (!dbmsg || !msg) {
    return;
  }
  memset(dbmsg, 0, sizeof(CIDBMessage));
  dbmsg->cmd = msg->header.msgCommand;
  dbmsg->subcmd = msg->header.msgSubcommand;
  if (dbmsg->subcmd != CIDBMSG_SUBCMD_QUERY && dbmsg->subcmd != CIDBMSG_SUBCMD_RESP) {
    return;
  }

  switch (dbmsg->cmd) {
    case CIDBMSG_CMD_REGISTER:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        /* nothing to read */
      }
      else {
        dbmsg->errcode = CIDB_GET_USHORT(msg->data, 0);
        dbmsg->clientid = CIDB_GET_USHORT(msg->data, 2);
      }
      break;
    case CIDBMSG_CMD_CALLER_LIST:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        dbmsg->userid = CIDB_GET_USHORT(msg->data, 0);
        dbmsg->stringlist = malloc(sizeof(unsigned char*));
        dbmsg->nstrings = 1;
        cidbmsg_read_string(&msg->data[2], &dbmsg->stringlist[0]);
      }
      else {
        dbmsg->errcode = CIDB_GET_USHORT(msg->data, 0);
        cidbmsg_read_table(&msg->data[2], &dbmsg->table); 
      }
      break;
    case CIDBMSG_CMD_READ_CALLER:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        dbmsg->userid = CIDB_GET_USHORT(msg->data, 0);
        dbmsg->flags  = CIDB_GET_USHORT(msg->data, 2);
        dbmsg->stringlist = malloc(sizeof(unsigned char*));
        dbmsg->nstrings = 1;
        cidbmsg_read_string(&msg->data[4], &dbmsg->stringlist[0]);
      }
      else {
        dbmsg->errcode = CIDB_GET_USHORT(msg->data, 0);
        cidbmsg_read_table(&msg->data[2], &dbmsg->table);
      }
      break;
    case CIDBMSG_CMD_WRITE_CALLER:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        dbmsg->userid = CIDB_GET_USHORT(msg->data, 0);
        dbmsg->flags = CIDB_GET_USHORT(msg->data, 2);
        cidbmsg_read_table(&msg->data[4], &dbmsg->table);
      }
      else {
        dbmsg->errcode = CIDB_GET_USHORT(msg->data, 0);
        dbmsg->index = CIDB_GET_ULONG(msg->data, 2);
      }
      break;
    case CIDBMSG_CMD_WRITE_CALL:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        cidbmsg_read_table(msg->data, &dbmsg->table);
      }
      else {
        dbmsg->errcode = CIDB_GET_USHORT(msg->data, 0);
        dbmsg->index = CIDB_GET_ULONG(msg->data, 2);
      }
      break;
    case CIDBMSG_CMD_CALL_LIST:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        dbmsg->offset = CIDB_GET_ULONG(msg->data, 0);
        dbmsg->count = CIDB_GET_USHORT(msg->data, 4);
      }
      else {
        dbmsg->errcode = CIDB_GET_USHORT(msg->data, 0);
        cidbmsg_read_table(&msg->data[2], &dbmsg->table);
      }
      break;
    case CIDBMSG_CMD_SHOWTABLES:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
      }
      else {
        dbmsg->errcode = CIDB_GET_USHORT(msg->data, 0);
        dbmsg->nstrings = CIDB_GET_USHORT(msg->data, 2);
        if (dbmsg->nstrings) {
          pos = 4;
          dbmsg->stringlist = malloc(sizeof(unsigned char*)*dbmsg->nstrings);
          for (i = 0; i < dbmsg->nstrings; i++) {
            pos += cidbmsg_read_string(&msg->data[pos], &dbmsg->stringlist[i]);
          }
        }
      }
      break;
    case CIDBMSG_CMD_DESCRIBE_TABLE:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        dbmsg->nstrings = 1;
        dbmsg->stringlist = malloc(sizeof(unsigned char*));
        cidbmsg_read_string(msg->data, &dbmsg->stringlist[0]);
      }
      else {
        dbmsg->errcode = CIDB_GET_USHORT(msg->data, 0);
        dbmsg->nstrings = CIDB_GET_USHORT(msg->data, 2);
        if (dbmsg->nstrings) {
          pos = 4;
          dbmsg->stringlist = malloc(sizeof(unsigned char*)*dbmsg->nstrings);
          for (i = 0; i < dbmsg->nstrings; i++) {
            pos += cidbmsg_read_string(&msg->data[pos], &dbmsg->stringlist[i]);
          }
        }
      }
      break;
    case CIDBMSG_CMD_DELETE_CALLER:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        dbmsg->userid = CIDB_GET_USHORT(msg->data, 0);
        dbmsg->nstrings = CIDB_GET_USHORT(msg->data, 2);
        pos = 4;
        if(dbmsg->nstrings) {
          dbmsg->stringlist = malloc(sizeof(unsigned char*)*dbmsg->nstrings);
          for (i = 0; i < dbmsg->nstrings; i++) {
            pos += cidbmsg_read_string(&msg->data[pos], &dbmsg->stringlist[i]);
          }
        }
      }
      else {
        dbmsg->errcode = CIDB_GET_USHORT(msg->data, 0);
      }
      break;
    case CIDBMSG_CMD_LIST_INFO:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
      }
      else {
        dbmsg->errcode = CIDB_GET_USHORT(msg->data, 0);
        dbmsg->maxentries = CIDB_GET_ULONG(msg->data, 2);
      }
      break;
    case CIDBMSG_CMD_LISTEN:
      if (dbmsg->subcmd == CIDBMSG_SUBCMD_QUERY) {
        dbmsg->mask = CIDB_GET_ULONG(msg->data, 0);
      }
      else {
        dbmsg->errcode = CIDB_GET_USHORT(msg->data, 0);
      }
      break;
    default:
      return;
  }
}

void cidbmsg_table_free(CIDBTable * table) {
  if (!table) return;
  unsigned short int i;
  if (table->column_names) {
    for (i = 0; i < table->ncols; i++) {
      if (table->column_names[i]) free(table->column_names[i]);
    }
    free(table->column_names);
  }
  if (table->fields) {
    for (i = 0; i < table->ncols*table->nrows; i++) {
      if (table->fields[i]) free(table->fields[i]);
    }
    free(table->fields);
  }
  memset(table, 0, sizeof(CIDBTable)); 
}
