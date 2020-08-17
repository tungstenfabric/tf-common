/*
 * Copyright 2019, Juniper Networks Inc.
 */

#ifndef __KERNEL__

#include "sandesh.h"

static char *
thriftTypeToStr (ThriftType t)
{
    switch (t) {
    case T_BOOL: return "bool";
    case T_I08: return "i8";
    case T_I16: return "i16";
    case T_I32: return "i32";
    case T_U64: return "u64";
    case T_I64: return "i64";
    case T_DOUBLE: return "double";
    case T_STRING: return "string";
    case T_STRUCT: return "struct";
    case T_MAP: return "map";
    case T_SET: return "set";
    case T_LIST: return "list";
    case T_UTF8: return "utf8";
    case T_UTF16: return "utf16";
    case T_U16: return "u16";
    case T_U32: return "u32";
    case T_XML: return "xml";
    case T_IPV4: return "ipv4";
    case T_UUID: return "uuid_t";
    case T_IPADDR: return "ipaddr";
    default:  return "unknown";
    }
    return "unknown";
}

static int
stack_push (ThriftXMLProtocol *xml_proto, int type, const char *tag)
{
    int *tos = &xml_proto->tag_stacks[type].tos;
    if (((*tos) + 1) >= MAX_TAG_STACK_SIZE) {
        /* stack full */
        return -1;
    }
    (*tos)++;
    strcpy(xml_proto->tag_stacks[type].stack[(*tos)], tag);
    return 1;
}

static int
stack_pop (ThriftXMLProtocol *xml_proto, int type, char *tag)
{
    int *tos = &xml_proto->tag_stacks[type].tos;
    if ((*tos) < 0) {
        /* stack empty */
        return -1;
    }
    strcpy(tag, xml_proto->tag_stacks[type].stack[(*tos)]);
    (*tos)--;
    return 1;
}

/* Write methods */

static int32_t
thrift_xml_protocol_write_byte (ThriftProtocol *protocol, const int8_t value,
                                int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];
    /* To avoid printing illegible chars, convert to u_int32 and print in hex */
    const u_int32_t val = (const u_int32_t) ((const u_int8_t)value);

    size = snprintf(buf, MAX_XML_BUFFER_SIZE, "%x\n", val);
    if (size > 0) {
        if (thrift_transport_write (protocol->transport,
                                    (const void *) buf, size, error))
        {
            return size;
        }
    }
    return -1;
}

static int32_t
thrift_xml_protocol_write_i16 (ThriftProtocol *protocol, const int16_t value,
                               int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];

    size = snprintf(buf, MAX_XML_BUFFER_SIZE, "%hd\n", value);
    if (size > 0) {
        if (thrift_transport_write (protocol->transport,
                                    (const void *) buf, size, error))
        {
            return size;
        }
    }
    return -1;
}

static int32_t
thrift_xml_protocol_write_u16 (ThriftProtocol *protocol, const u_int16_t value,
                               int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];

    size = snprintf(buf, MAX_XML_BUFFER_SIZE, "%hu\n", value);
    if (size > 0) {
        if (thrift_transport_write (protocol->transport,
                                    (const void *) buf, size, error))
        {
            return size;
        }
    }
    return -1;
}

static int32_t
thrift_xml_protocol_write_i32 (ThriftProtocol *protocol, const int32_t value,
                               int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];

    size = snprintf(buf, MAX_XML_BUFFER_SIZE, "%d\n", value);
    if (size > 0) {
        if (thrift_transport_write (protocol->transport,
                                    (const void *) buf, size, error))
        {
            return size;
        }
    }
    return -1;
}

static int32_t
thrift_xml_protocol_write_u32 (ThriftProtocol *protocol, const u_int32_t value,
                               int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];

    size = snprintf(buf, MAX_XML_BUFFER_SIZE, "%u\n", value);
    if (size > 0) {
        if (thrift_transport_write (protocol->transport,
                                    (const void *) buf, size, error))
        {
            return size;
        }
    }
    return -1;
}

static int32_t
thrift_xml_protocol_write_i64 (ThriftProtocol *protocol, const int64_t value,
                               int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];

    size = snprintf(buf, MAX_XML_BUFFER_SIZE, "%ld\n", value);
    if (size > 0) {
        if (thrift_transport_write (protocol->transport,
                                    (const void *) buf, size, error))
        {
            return size;
        }
    }
    return -1;
}

static int32_t
thrift_xml_protocol_write_u64 (ThriftProtocol *protocol, const uint64_t value,
                               int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];

    size = snprintf(buf, MAX_XML_BUFFER_SIZE, "%lu\n", value);
    if (size > 0) {
        if (thrift_transport_write (protocol->transport,
                                    (const void *) buf, size, error))
        {
            return size;
        }
    }
    return -1;
}

static int32_t
thrift_xml_protocol_write_bool (ThriftProtocol *protocol,
                                const u_int8_t value, int *error)
{
  u_int8_t tmp = value ? 1 : 0;
  return thrift_xml_protocol_write_byte (protocol, tmp, error);
}

static inline int
write_xml_open_tag (const char *tag, char *buf, int buf_size, int newline)
{
    int ret;
    ret = snprintf(buf, buf_size, "<%s>%c", tag, (newline? '\n' : ' '));
    return ret;
}

static inline int
write_xml_close_tag (const char *tag, char *buf, int buf_size, int newline)
{
    int ret;
    ret = snprintf(buf, buf_size, "</%s>%c", tag, (newline? '\n' : ' '));
    return ret;
}

static inline int
write_xml_open_tag_with_attr (const char *tag, const char *attr1,
                              const char *attr1_value, const char *attr2,
                              const char *attr2_value, char *buf, int buf_size,
                              int newline)
{
    int ret;
    ret = snprintf(buf, buf_size, "<%s %s=\"%s\" %s=\"%s\">%c", tag, attr1,
                   attr1_value, attr2, attr2_value, (newline? '\n' : ' '));
    return ret;
}

static int32_t
thrift_xml_protocol_write_sandesh_begin (ThriftProtocol *protocol,
                                         const char *name, int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];
    ThriftXMLProtocol *xml_proto = (ThriftXMLProtocol *) protocol;

    size = write_xml_open_tag(name, buf, MAX_XML_BUFFER_SIZE, TRUE);
    if (size > 0) {
        if (thrift_transport_write(protocol->transport,
                                   (const void *) buf, size, error)) {
            stack_push(xml_proto, TAG_SANDESH, name);
            return size;
        }
    }
    return -1;
}

static int32_t
thrift_xml_protocol_write_sandesh_end (ThriftProtocol *protocol,
                                       int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];
    char tag[MAX_XML_TAG_SIZE];
    ThriftXMLProtocol *xml_proto = (ThriftXMLProtocol *) protocol;

    stack_pop(xml_proto, TAG_SANDESH, tag);
    size = write_xml_close_tag(tag, buf, MAX_XML_BUFFER_SIZE, TRUE);
    if (size > 0) {
        if (thrift_transport_write(protocol->transport,
                                   (const void *) buf, size, error)) {
            return size;
        }
    }
    return -1;
}

static int32_t
thrift_xml_protocol_write_struct_begin (ThriftProtocol *protocol,
                                        const char *name,
                                        int *error)
{
    /* Not required to be implemented */
    return 0;
}

static int32_t
thrift_xml_protocol_write_struct_end (ThriftProtocol *protocol,
                                      int *error)
{
    /* Not required to be implemented */
    return 0;
}


static int32_t
thrift_xml_protocol_write_field_begin (ThriftProtocol *protocol,
                                       const char *name,
                                       const ThriftType field_type,
                                       const int16_t field_id,
                                       int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];
    char field_id_str[MAX_XML_BUFFER_SIZE];
    ThriftXMLProtocol *xml_proto = (ThriftXMLProtocol *) protocol;

    snprintf(field_id_str, MAX_XML_BUFFER_SIZE, "%hd", field_id);
    size = write_xml_open_tag_with_attr(name, "type",
                                        thriftTypeToStr(field_type),
                                        "identifier",
                                        field_id_str, buf,
                                        MAX_XML_BUFFER_SIZE, TRUE);
    if (size > 0) {
        if (thrift_transport_write(protocol->transport,
                                   (const void *) buf, size, error)) {
            stack_push(xml_proto, TAG_FIELD, name);
            return size;
        }
    }
    return -1;
}

static int32_t
thrift_xml_protocol_write_field_end (ThriftProtocol *protocol,
                                     int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];
    char tag[MAX_XML_TAG_SIZE];
    ThriftXMLProtocol *xml_proto = (ThriftXMLProtocol *) protocol;

    stack_pop(xml_proto, TAG_FIELD, tag);
    size = write_xml_close_tag(tag, buf, MAX_XML_BUFFER_SIZE, TRUE);
    if (size > 0) {
        if (thrift_transport_write(protocol->transport,
                                   (const void *) buf, size, error)) {
            return size;
        }
    }
    return -1;
}


static int32_t
thrift_xml_protocol_write_field_stop (ThriftProtocol *protocol,
                                      int *error)
{
    /* Not required to be implemented */
    return 0;
}

static int32_t
thrift_xml_protocol_write_list_begin (ThriftProtocol *protocol,
                                      const ThriftType element_type,
                                      const u_int32_t list_size,
                                      int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];
    char list_size_str[MAX_XML_BUFFER_SIZE];
    ThriftXMLProtocol *xml_proto = (ThriftXMLProtocol *) protocol;

    snprintf(list_size_str, MAX_XML_BUFFER_SIZE, "%u", list_size);
    size = write_xml_open_tag_with_attr("list", "type",
                                        thriftTypeToStr(element_type),
                                        "size",
                                        list_size_str, buf,
                                        MAX_XML_BUFFER_SIZE, TRUE);
    if (size > 0) {
        if (thrift_transport_write(protocol->transport,
                                   (const void *) buf, size, error)) {
            stack_push(xml_proto, TAG_LIST, "list");
            return size;
        }
    }
    return -1;
}

static int32_t
thrift_xml_protocol_write_list_end (ThriftProtocol *protocol, int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];
    char tag[MAX_XML_TAG_SIZE];
    ThriftXMLProtocol *xml_proto = (ThriftXMLProtocol *) protocol;

    stack_pop(xml_proto, TAG_LIST, tag);
    size = write_xml_close_tag(tag, buf, MAX_XML_BUFFER_SIZE, TRUE);
    if (size > 0) {
        if (thrift_transport_write(protocol->transport,
                                   (const void *) buf, size, error)) {
            return size;
        }
    }
    return -1;
}

/* convert value in network byte order to ip addr string */
static int
ip_address_to_str (int family, const void *value, char *buf, int buf_size)
{
    const char *addr = (const char *) value;

    if (family == AF_INET) {
        return snprintf(buf, buf_size, "%d.%d.%d.%d\n", (u_int8_t)addr[3],
                        (u_int8_t) addr[2], (u_int8_t) addr[1],
                        (u_int8_t) addr[0]);
    } else {
        return snprintf(buf, buf_size, "%x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x\n",
                        (u_int8_t) addr[15], (u_int8_t) addr[14],
                        (u_int8_t) addr[13], (u_int8_t) addr[12],
                        (u_int8_t) addr[11], (u_int8_t) addr[10],
                        (u_int8_t) addr[9], (u_int8_t) addr[8],
                        (u_int8_t) addr[7], (u_int8_t) addr[6],
                        (u_int8_t) addr[5], (u_int8_t) addr[4],
                        (u_int8_t) addr[3], (u_int8_t) addr[2],
                        (u_int8_t) addr[1], (u_int8_t) addr[0]);
    }
}

static int32_t
thrift_xml_protocol_write_ipvx (ThriftProtocol *protocol, int family, const void *value,
                                int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];

    size = ip_address_to_str(family, value, buf, MAX_XML_BUFFER_SIZE);
    if (size > 0) {
        if (thrift_transport_write (protocol->transport,
                                    (const void *) buf, size, error))
        {
            return size;
        }
    }
    return -1;
}

/* Assumes value to be in network byte order */
static int32_t
thrift_xml_protocol_write_ipv4 (ThriftProtocol *protocol, const u_int32_t value,
                                int *error)
{
    return thrift_xml_protocol_write_ipvx(protocol, AF_INET, &value, error);
}

/* Assumes value to be in network byte order */
static int32_t
thrift_xml_protocol_write_ipaddr (ThriftProtocol *protocol,
                                  const ipaddr_t *value, int *error)
{
    if (value->iptype == AF_INET) {
        return thrift_xml_protocol_write_ipvx(protocol, AF_INET,
                                              &value->ipv4, error);
    } else {
        return thrift_xml_protocol_write_ipvx(protocol, AF_INET6,
                                              &value->ipv6, error);
    }
}

static int32_t
thrift_xml_protocol_write_uuid_t (ThriftProtocol *protocol, const ct_uuid_t value,
                                  int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];
    const char *p = (const char *)&value;

    /* write in 8-4-4-4-12 format */
    size = snprintf(buf, MAX_XML_BUFFER_SIZE,
                    "%x%x%x%x-%x%x-%x%x-%x%x-%x%x%x%x%x%x\n",
                    (u_int8_t) p[0], (u_int8_t) p[1], (u_int8_t) p[2],
                    (u_int8_t) p[3], (u_int8_t) p[4], (u_int8_t) p[5],
                    (u_int8_t) p[6], (u_int8_t) p[7], (u_int8_t) p[8],
                    (u_int8_t) p[9], (u_int8_t) p[10], (u_int8_t) p[11],
                    (u_int8_t) p[12], (u_int8_t) p[13], (u_int8_t) p[14],
                    (u_int8_t) p[15]);
    if (size > 0) {
        if (thrift_transport_write (protocol->transport,
                                    (const void *) buf, size, error))
        {
            return size;
        }
    }
    return -1;
}

static int32_t
thrift_xml_protocol_write_double (ThriftProtocol *protocol,
                                  const double value, int *error)
{
    int size;
    char buf[MAX_XML_BUFFER_SIZE];

    size = snprintf(buf, MAX_XML_BUFFER_SIZE, "%lf\n", value);
    if (size > 0) {
        if (thrift_transport_write (protocol->transport,
                                    (const void *) buf, size, error))
        {
            return size;
        }
    }
    return -1;
}

static int
isXMLEscChar (char c)
{
    switch (c) {
        case '&':
        case '\\':
        case '>':
        case '<':
           return 1;
        default:
           return 0;
    }
}

static int32_t
thrift_xml_protocol_write_string (ThriftProtocol *protocol,
                                  const char *str, int *error)
{
    int size = 0, i = 0, j = 0;
    char buf[MAX_XML_BUFFER_SIZE];
    const char *amp = "&amp";
    const char *bs = "&apos";
    const char *lt = "&lt";
    const char *gt = "&gt";

    if (!str) {
        return -1;
    }

    while (str[i]) {
        if ((j == MAX_XML_BUFFER_SIZE) || (isXMLEscChar(str[i]) && (j > 0))) {
            if (!thrift_transport_write (protocol->transport,
                                    (const void *) buf, j, error)) {
                 return -1;
            }
            size += j;
            j = 0;
        }
        switch (str[i]) {
            case '&':
                strcpy(&(buf[j]), amp);
                j += 4;
                break;
            case '\\':
                strcpy(&(buf[j]), bs);
                j += 5;
                break;
            case '<':
                strcpy(&(buf[j]), lt);
                j += 3;
                break;
            case '>':
                strcpy(&(buf[j]), gt);
                j += 3;
                break;
            default:
                buf[j] = str[i];
                j++;
                break;
        }
        i++;
    }
    if (j) {
        if (!thrift_transport_write (protocol->transport,
                                    (const void *) buf, j, error)) {
             return -1;
        }
        size += j;
    }
    return size;
}

static int32_t
thrift_xml_protocol_write_binary (ThriftProtocol *protocol,
                                  const void * buf,
                                  const u_int32_t len, int *error)
{
    if (thrift_transport_write (protocol->transport,
                                (const void *) buf, len, error))
    {
        return len;
    }
    return -1;
}

/* Read methods */
static int32_t
thrift_xml_protocol_read_sandesh_begin (ThriftProtocol *protocol,
                                        char **name,
                                        int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (name);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_sandesh_end (ThriftProtocol *protocol, int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_struct_begin (ThriftProtocol *protocol ,
                                       char **name,
                                       int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (name);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_struct_end (ThriftProtocol *protocol, int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_field_begin (ThriftProtocol *protocol,
                                      char **name,
                                      ThriftType *field_type,
                                      int16_t *field_id,
                                      int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (name);
    THRIFT_UNUSED_VAR (field_type);
    THRIFT_UNUSED_VAR (field_id);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_field_end (ThriftProtocol *protocol ,
                                    int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_list_begin (ThriftProtocol *protocol,
                                     ThriftType *element_type,
                                     u_int32_t *size, int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (element_type);
    THRIFT_UNUSED_VAR (size);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_list_end (ThriftProtocol *protocol ,
                                   int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_bool (ThriftProtocol *protocol, u_int8_t *value,
                               int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (value);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_byte (ThriftProtocol *protocol, int8_t *value,
                               int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (value);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_i16 (ThriftProtocol *protocol, int16_t *value,
                              int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (value);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_i32 (ThriftProtocol *protocol, int32_t *value,
                              int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (value);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_i64 (ThriftProtocol *protocol, int64_t *value,
                              int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (value);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_u16 (ThriftProtocol *protocol, u_int16_t *value,
                              int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (value);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_u32 (ThriftProtocol *protocol, u_int32_t *value,
                              int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (value);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_u64 (ThriftProtocol *protocol, uint64_t *value,
                              int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (value);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_ipv4 (ThriftProtocol *protocol, u_int32_t *value,
                               int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (value);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_ipaddr (ThriftProtocol *protocol, ipaddr_t *value,
                                 int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (value);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_uuid_t (ThriftProtocol *protocol, ct_uuid_t *value,
                                 int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (value);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_double (ThriftProtocol *protocol,
                                 double *value, int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (value);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_string (ThriftProtocol *protocol,
                                 char **str, int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (str);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

static int32_t
thrift_xml_protocol_read_binary (ThriftProtocol *protocol,
                                 void **buf, u_int32_t *len,
                                 int *error)
{
    THRIFT_UNUSED_VAR (protocol);
    THRIFT_UNUSED_VAR (buf);
    THRIFT_UNUSED_VAR (len);
    THRIFT_UNUSED_VAR (error);
    return -1;
}

void
thrift_xml_protocol_init (ThriftProtocol *protocol)
{
    ThriftXMLProtocol *xml_protocol = (ThriftXMLProtocol *) protocol;
    int i;

    for (i = 0; i < TAG_MAX; i++) {
         xml_protocol->tag_stacks[i].tos = -1;
    }

    protocol->write_sandesh_begin = thrift_xml_protocol_write_sandesh_begin;
    protocol->write_sandesh_end = thrift_xml_protocol_write_sandesh_end;
    protocol->write_struct_begin = thrift_xml_protocol_write_struct_begin;
    protocol->write_struct_end = thrift_xml_protocol_write_struct_end;
    protocol->write_field_begin = thrift_xml_protocol_write_field_begin;
    protocol->write_field_end = thrift_xml_protocol_write_field_end;
    protocol->write_field_stop = thrift_xml_protocol_write_field_stop;
    protocol->write_list_begin = thrift_xml_protocol_write_list_begin;
    protocol->write_list_end = thrift_xml_protocol_write_list_end;
    protocol->write_bool = thrift_xml_protocol_write_bool;
    protocol->write_byte = thrift_xml_protocol_write_byte;
    protocol->write_i16 = thrift_xml_protocol_write_i16;
    protocol->write_i32 = thrift_xml_protocol_write_i32;
    protocol->write_i64 = thrift_xml_protocol_write_i64;
    protocol->write_u16 = thrift_xml_protocol_write_u16;
    protocol->write_u32 = thrift_xml_protocol_write_u32;
    protocol->write_u64 = thrift_xml_protocol_write_u64;
    protocol->write_ipv4 = thrift_xml_protocol_write_ipv4;
    protocol->write_ipaddr = thrift_xml_protocol_write_ipaddr;
    protocol->write_double = thrift_xml_protocol_write_double;
    protocol->write_string = thrift_xml_protocol_write_string;
    protocol->write_binary = thrift_xml_protocol_write_binary;
    protocol->write_xml = thrift_xml_protocol_write_string;
    protocol->write_uuid_t = thrift_xml_protocol_write_uuid_t;
    protocol->read_sandesh_begin = thrift_xml_protocol_read_sandesh_begin;
    protocol->read_sandesh_end = thrift_xml_protocol_read_sandesh_end;
    protocol->read_struct_begin = thrift_xml_protocol_read_struct_begin;
    protocol->read_struct_end = thrift_xml_protocol_read_struct_end;
    protocol->read_field_begin = thrift_xml_protocol_read_field_begin;
    protocol->read_field_end = thrift_xml_protocol_read_field_end;
    protocol->read_list_begin = thrift_xml_protocol_read_list_begin;
    protocol->read_list_end = thrift_xml_protocol_read_list_end;
    protocol->read_bool = thrift_xml_protocol_read_bool;
    protocol->read_byte = thrift_xml_protocol_read_byte;
    protocol->read_i16 = thrift_xml_protocol_read_i16;
    protocol->read_i32 = thrift_xml_protocol_read_i32;
    protocol->read_i64 = thrift_xml_protocol_read_i64;
    protocol->read_u16 = thrift_xml_protocol_read_u16;
    protocol->read_u32 = thrift_xml_protocol_read_u32;
    protocol->read_u64 = thrift_xml_protocol_read_u64;
    protocol->read_ipv4 = thrift_xml_protocol_read_ipv4;
    protocol->read_ipaddr = thrift_xml_protocol_read_ipaddr;
    protocol->read_double = thrift_xml_protocol_read_double;
    protocol->read_string = thrift_xml_protocol_read_string;
    protocol->read_binary = thrift_xml_protocol_read_binary;
    protocol->read_xml = thrift_xml_protocol_read_string;
    protocol->read_uuid_t = thrift_xml_protocol_read_uuid_t;
}

#endif


