#ifndef __CIDBMESSAGES_H__
#define __CIDBMESSAGES_H__
/**@todo add endian conversion (always use big endian) */

#include <sys/types.h>

/** @brief Commands */
/* @{ */
/** @brief Register a client
 *
 *  query: [Header]
 *  resp : [Header] [ushort errcode] [ushort clientid]
 */
#define CIDBMSG_CMD_REGISTER            0x01

/** @brief Retrieve a list of callers for a user
 *
 *  query: [Header] [ushort userid] [string filter] !!filter is new
 *  resp : [Header] [ushort errcode] [table]
 */
#define CIDBMSG_CMD_CALLER_LIST         0x02

/** @brief Find a caller in the list
 *
 *  query: [Header] [ushort userid] [ushort flags] [string number]
 *  resp : [Header] [ushort errcode] [table]
 */
#define CIDBMSG_CMD_READ_CALLER         0x03

/** @brief Add or update a caller in the list
 *
 *  query: [Header] [ushort userid] [ushort flags] [table]
 *  resp : [Header] [ushort errcode] [ulong id]
 */
#define CIDBMSG_CMD_WRITE_CALLER        0x04

/** @brief Write call information to the list
 *
 *  query: [Header] [table]
 *  resp : [Header] [ushort errcode] [ulong id]
 */
#define CIDBMSG_CMD_WRITE_CALL          0x05

/** @brief Read the call list
 *
 *  query: [Header] [ulong offset] [ushort count]
 *  resp : [Header] [ushort errcode] [table]
 */
#define CIDBMSG_CMD_CALL_LIST           0x06

/** @brief Get a list of the tables
 *
 *  query: [Header]
 *  resp : [Header] [ushort errcode] [ushort ntables] [strings ..]
 */
#define CIDBMSG_CMD_SHOWTABLES          0x07

/** @brief Describe the table
 *
 *  query: [Header] [string tablename]
 *  resp : [Header] [ushort errcode] [ushort ncols] [strings ..]
 */
#define CIDBMSG_CMD_DESCRIBE_TABLE      0x08

/** @brief
 *
 *  query: [Header] [ushort userid] [ushort nstrings=2] [string number] [string name]
 *  resp : [Header] [ushort errcode]
 */
#define CIDBMSG_CMD_DELETE_CALLER       0x09

/** @brief
 *
 *  query: [Header]
 *  resp : [Header] [ushort errcode] [ulong numentries]
 */
#define CIDBMSG_CMD_LIST_INFO           0x0a

/** @brief Listen to specific events
 *
 *  query: [Header] [ulong mask]
 *  resp : [Header] [ushort errcode]
 */
#define CIDBMSG_CMD_LISTEN              0x0b
/* @} */

#define CIDBMSG_SUBCMD_QUERY            0x80
#define CIDBMSG_SUBCMD_RESP             0x81

static inline void _cidb_set_uchar(void *dst, int off, unsigned short val)
{
    ((unsigned char *)dst)[off  ] = val & 0xff;
}

static inline void _cidb_set_ushort(void *dst, int off, unsigned short val)
{
    ((unsigned char *)dst)[off  ] = val & 0xff;
    ((unsigned char *)dst)[off+1] = (val >> 8) & 0xff;
}

static inline void _cidb_set_ulong(void *dst, int off, unsigned int val)
{
    ((unsigned char *)dst)[off  ] = val & 0xff;
    ((unsigned char *)dst)[off+1] = (val >> 8) & 0xff;
    ((unsigned char *)dst)[off+2] = (val >> 16) & 0xff;
    ((unsigned char *)dst)[off+3] = (val >> 24) & 0xff;
}

#define CIDB_SET_UCHAR(dst, off, val) _cidb_set_uchar(dst, off, val)
#define CIDB_SET_USHORT(dst, off, val) _cidb_set_ushort(dst, off, val)
#define CIDB_SET_ULONG(dst, off, val) _cidb_set_ulong(dst, off, val)

#define CIDB_GET_UCHAR(dst, off)  (((unsigned char*)dst)[off] & 0xff)
#define CIDB_GET_USHORT(dst, off) ((((unsigned char*)dst)[off] & 0xff) | ((((unsigned char*)dst)[off+1] & 0xff) << 8))
#define CIDB_GET_ULONG(dst, off)  ((((unsigned char*)dst)[off] & 0xff) | ((((unsigned char*)dst)[off+1] & 0xff) << 8) | \
                                  ((((unsigned char*)dst)[off+2] & 0xff) << 16) | ((((unsigned char*)dst)[off+3] & 0xff) << 24))


typedef struct _CIDBMsgHeader {
    unsigned short int ClientId;     /**< The client identifier of the connection */
    unsigned char msgCommand;        /**< The command of the message */
    unsigned char msgSubcommand;     /**< The subcommand of the message */
    unsigned int msgId;         /**< The message identifier */
    unsigned short int msgFlags;     /**< The flags concerning the message */
    unsigned int dataSize;      /**< The total size of the data */
    unsigned int partSize;      /**< The size of the data in the current package */
    unsigned int offset;        /**< The offset of the part in the total message */
} CIDBMsgHeader;

typedef struct _CIDBMsg {
    CIDBMsgHeader header;
    unsigned char *data;
} CIDBMsg;

typedef struct _CIDBMsgTransmission {
    CIDBMsg *completeMsg;
    unsigned int offset;
} CIDBMsgTransmission;


/*#define CIDBMSG_MSG_TOTAL_SIZE  (2048+128)*/
#define CIDBMSG_MSG_TOTAL_SIZE  (2048)
#define CIDBMSG_HEADER_SIZE     (22)
#define CIDBMSG_DATA_SIZE       (CIDBMSG_MSG_TOTAL_SIZE-CIDBMSG_HEADER_SIZE)

#define CIDBMSG_HEADER_SET_CLIENTID(h, val)       CIDB_SET_USHORT(h, 0, val)
#define CIDBMSG_HEADER_SET_MSGCOMMAND(h, val)     CIDB_SET_UCHAR(h, 2, val)
#define CIDBMSG_HEADER_SET_MSGSUBCOMMAND(h, val)  CIDB_SET_UCHAR(h, 3, val)
#define CIDBMSG_HEADER_SET_MSGID(h, val)          CIDB_SET_ULONG(h, 4, val)
#define CIDBMSG_HEADER_SET_MSGFLAGS(h, val)       CIDB_SET_USHORT(h, 8, val)
#define CIDBMSG_HEADER_SET_DATASIZE(h, val)       CIDB_SET_ULONG(h, 10, val)
#define CIDBMSG_HEADER_SET_PARTSIZE(h, val)       CIDB_SET_ULONG(h, 14, val)
#define CIDBMSG_HEADER_SET_OFFSET(h, val)         CIDB_SET_ULONG(h, 18, val)

#define CIDBMSG_HEADER_GET_CLIENTID(h)            CIDB_GET_USHORT(h, 0)
#define CIDBMSG_HEADER_GET_MSGCOMMAND(h)          CIDB_GET_UCHAR(h, 2)
#define CIDBMSG_HEADER_GET_MSGSUBCOMMAND(h)       CIDB_GET_UCHAR(h, 3)
#define CIDBMSG_HEADER_GET_MSGID(h)               CIDB_GET_ULONG(h, 4)
#define CIDBMSG_HEADER_GET_MSGFLAGS(h)            CIDB_GET_USHORT(h, 8)
#define CIDBMSG_HEADER_GET_DATASIZE(h)            CIDB_GET_ULONG(h, 10)
#define CIDBMSG_HEADER_GET_PARTSIZE(h)            CIDB_GET_ULONG(h, 14)
#define CIDBMSG_HEADER_GET_OFFSET(h)             CIDB_GET_ULONG(h, 18)

typedef struct _CIDBTable {
    unsigned short int nrows;
    unsigned short int ncols;
    unsigned char **column_names;
    unsigned char **fields;
} CIDBTable;

typedef struct _CIDBMessage {
    unsigned char cmd;
    unsigned char subcmd;
    unsigned int msgid;
    unsigned short int clientid;
    unsigned short int errcode;
    unsigned short int userid;
    unsigned int flags;
    unsigned short nstrings;
    unsigned char **stringlist;
    CIDBTable table;
    unsigned int index;
    unsigned int offset;
    unsigned short int count;
    unsigned int mask;
    unsigned int maxentries;
} CIDBMessage;

unsigned int cidbmsg_prepare_header(unsigned char *dst, CIDBMsgHeader *header);
unsigned int cidbmsg_read_header(CIDBMsgHeader *header, unsigned char *src);
unsigned int cidbmsg_read_table(unsigned char *msg, CIDBTable *table);
unsigned int cidbmsg_table_get_size(CIDBTable *table);
unsigned int cidbmsg_write_table(unsigned char *msg, CIDBTable *table);
unsigned int cidbmsg_write_string(unsigned char *msg, unsigned char *str);
unsigned int cidbmsg_read_string(unsigned char *msg, unsigned char **str);

unsigned int cidbmsg_transmission_start(unsigned char *part, CIDBMsgTransmission *msg);
unsigned int cidbmsg_transmission_continue(unsigned char *part, CIDBMsgTransmission *msg);

void cidbmsg_mkmessage(unsigned short int clientid,
                       unsigned char command,
                       unsigned char subcommand,
                       unsigned int msgid,
                       CIDBMsgHeader *header);

unsigned int cidbmsg_write_message(CIDBMessage *dbmsg, CIDBMsg *msg);
void cidbmsg_read_message(CIDBMsg *msg, CIDBMessage *dbmsg);

void cidbmsg_table_free(CIDBTable *table);

#endif