/*
 * Copyright 2019, Juniper Networks Inc.
 */

#ifndef __KERNEL__

#include "sandesh.h"

static void
dump_buf (char *msg, char *buf, int len)
{
#ifndef __KERNEL__
    int i;
    fprintf(stdout, "%s: len %d: ", msg, len);
    for (i = 0; i < len; i++) {
         fprintf(stdout, "%c", buf[i]);
    }
    fprintf(stdout, "\n");
#endif
}

/* implements thrift_transport_read */
int32_t
thrift_file_transport_read (ThriftTransport *transport, void *buf,
                            u_int32_t len, int *error)
{
    size_t ret;
    ThriftFileTransport *ft = (ThriftFileTransport *) transport;

    if (!ft->fp) {
        os_log(OS_LOG_ERR, "Unable to read from file %s\n", ft->filename);
        return -1;
    }
    ret = fread(buf, 1, len, ft->fp);
    if (ret != len) {
        os_log(OS_LOG_ERR, "Unable to read %d bytes from file %s\n", len, ft->filename);
        return -1;
    }
    return (int32_t)len;
}

/* implements thrift_transport_read_end
 * called when read is complete.  nothing to do on our end. */
u_int8_t
thrift_file_transport_read_end (ThriftTransport *transport ,
                                int *error)
{
    THRIFT_UNUSED_VAR (transport);
    THRIFT_UNUSED_VAR (error);
    return 1;
}

/* implements thrift_transport_write */
u_int8_t
thrift_file_transport_write (ThriftTransport *transport,
                             const void *buf,
                             const u_int32_t len, int *error)
{
    size_t ret;
    ThriftFileTransport *ft = (ThriftFileTransport *) transport;

    if (!ft->fp) {
        os_log(OS_LOG_ERR, "Unable to write to file %s\n", ft->filename);
        return -1;
    }
    ret = fwrite(buf, 1, len, ft->fp);
    if (ret != len) {
        os_log(OS_LOG_ERR, "Unable to write %d bytes to file %s\n", len, ft->filename);
        return -1;
    }
    return 1;
}

/* initializes the instance */
int thrift_file_transport_init (ThriftFileTransport *ft, char *filename)
{
    ThriftTransport *transport = (ThriftTransport *) ft;

    if (strlen(filename) >= MAX_FILE_NAME) {
        os_log(OS_LOG_ERR, "Filename specified is greater or equal to %d\n",
               MAX_FILE_NAME);
        return -1;
    }
    memset(ft, 0, sizeof(*ft));
    transport->ttype = T_TRANSPORT_FILE;
    transport->read = thrift_file_transport_read;
    transport->write = thrift_file_transport_write;
    /* Open the file for rw */
    strcpy(ft->filename, filename);
    ft->fp = fopen(filename, "w+");
    if (!ft->fp) {
        os_log(OS_LOG_ERR, "Unable to open file %s\n", filename);
        return -1;
    }
    return 0;
}

/* destructor */
void thrift_file_transport_close (ThriftFileTransport *ft)
{
    if (fclose(ft->fp) != 0) {
        os_log(OS_LOG_ERR, "Error while closing file %s\n", ft->filename);
    }
    memset(ft, 0, sizeof(*ft));
}

#endif /* __KERNEL__ */

