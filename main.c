#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define BUFFER_SIZE 1500
#define MIN(x, y) ((x) <= (y) ? (x) : (y))

static const uint32_t QR_MASK = 0x8000;
static const uint32_t OPCODE_MASK = 0x7800;
static const uint32_t AA_MASK = 0x0400;
static const uint32_t TC_MASK = 0x0200;
static const uint32_t RD_MASK = 0x0100;
static const uint32_t RA_MASK = 0x0080;
static const uint32_t RCODE_MASK = 0x000F;

/* Response Type */
enum {
    Ok_ResponseType = 0,
    FormatError_ResponseType = 1,
    ServerFailure_ResponseType = 2,
    NameError_ResponseType = 3,
    NotImplemented_ResponseType = 4,
    Refused_ResponseType = 5
};

/* Resource Record Types */
enum {
    A_Resource_RecordType = 1,
    NS_Resource_RecordType = 2,
    CNAME_Resource_RecordType = 5,
    SOA_Resource_RecordType = 6,
    PTR_Resource_RecordType = 12,
    MX_Resource_RecordType = 15,
    TXT_Resource_RecordType = 16,
    AAAA_Resource_RecordType = 28,
    SRV_Resource_RecordType = 33
};

/* Operation Code */
enum {
    QUERY_OperationCode = 0, /* standard query */
    IQUERY_OperationCode = 1, /* inverse query */
    STATUS_OperationCode = 2, /* server status request */
    NOTIFY_OperationCode = 4, /* request zone transfer */
    UPDATE_OperationCode = 5 /* change resource records */
};

/* Response Code */
enum {
    NoError_ResponseCode = 0,
    FormatError_ResponseCode = 1,
    ServerFailure_ResponseCode = 2,
    NameError_ResponseCode = 3
};

/* Query Type */
enum {
    IXFR_QueryType = 251,
    AXFR_QueryType = 252,
    MAILB_QueryType = 253,
    MAILA_QueryType = 254,
    STAR_QueryType = 255
};

struct Question {
    char *qName;
    uint16_t qType;
    uint16_t qClass;
    struct Question *next;
};

union ResourceData {
    struct {
        uint8_t txt_data_len;
        char *txt_data;
    } txt_record;
    struct {
        uint8_t addr[4];
    } a_record;
    struct {
        uint8_t addr[16];
    } aaaa_record;
};

struct ResourceRecord {
    char *name;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rd_length;
    union ResourceData rd_data;
    struct ResourceRecord *next;
};

struct Message {
    uint16_t id; /* Identifier */

    uint16_t qr; /* Query/Response Flag */
    uint16_t opcode; /* Operation Code */
    uint16_t aa; /* Authoritative Answer Flag */
    uint16_t tc; /* Truncation Flag */
    uint16_t rd; /* Recursion Desired */
    uint16_t ra; /* Recursion Available */
    uint16_t rcode; /* Response Code */

    uint16_t qdCount; /* Question Count */
    uint16_t anCount; /* Answer Record Count */
    uint16_t nsCount; /* Authority Record Count */
    uint16_t arCount; /* Additional Record Count */

    struct Question *questions;
    struct ResourceRecord *answers;
    struct ResourceRecord *authorities;
    struct ResourceRecord *additionals;
};

bool get_A_Record(uint8_t addr[4], const char domain_name[]) {
    if(strcmp("foo.bar.com", domain_name) == 0) {
        addr[0] = 192;
        addr[1] = 168;
        addr[2] = 1;
        addr[3] = 1;
        return true;
    } else {
        return false;
    }
}

bool get_AAAA_Record(uint8_t addr[16], const char domain_name[]) {
    if(strcmp("foo.bar.com", domain_name) == 0) {
        addr[0] = 0xfe;
        addr[1] = 0x80;
        addr[2] = 0x00;
        addr[3] = 0x00;
        addr[4] = 0x00;
        addr[5] = 0x00;
        addr[6] = 0x00;
        addr[7] = 0x00;
        addr[8] = 0x00;
        addr[9] = 0x00;
        addr[10] = 0x00;
        addr[11] = 0x00;
        addr[12] = 0x00;
        addr[13] = 0x00;
        addr[14] = 0x00;
        addr[15] = 0x01;
        return true;
    } else {
        return false;
    }
}

bool get_TXT_Record(char **addr, const char domain_name[]) {
    if(strcmp("foo.bar.com", domain_name) == 0) {
        *addr = "abcdefg";
        return true;
    } else {
        return false;
    }
}

void print_hex(uint8_t *buf, size_t len) {
    int i;
    printf("%zu bytes:\n", len);
    for(i = 0; i < len; i += 1)
        printf("%02x ", buf[i]);
    printf("\n");
}

void print_resource_record(struct ResourceRecord *rr) {
    int i;
    while(rr) {
        printf("    ResourceRecord { name '%s', type %u, class %u, ttl %u, rd_length %u, ",
            rr->name,
            rr->type,
            rr->class,
            rr->ttl,
            rr->rd_length
     );

        union ResourceData *rd = &rr->rd_data;
        switch(rr->type) {
            case A_Resource_RecordType:
                printf("Address Resource Record { address ");

                for(i = 0; i < 4; i += 1)
                    printf("%s%u", (i ? "." : ""), rd->a_record.addr[i]);

                printf(" }");
                break;
            case AAAA_Resource_RecordType:
                printf("AAAA Resource Record { address ");

                for(i = 0; i < 16; i += 1)
                    printf("%s%02x", (i ? ":" : ""), rd->aaaa_record.addr[i]);

                printf(" }");
                break;
            case TXT_Resource_RecordType:
                printf("Text Resource Record { txt_data '%s' }",
                    rd->txt_record.txt_data
                );
                break;
            default:
                printf("Unknown Resource Record { ??? }");
        }
        printf("}\n");
        rr = rr->next;
    }
}

void print_message(struct Message *msg) {
    struct Question *q;

    printf("QUERY { ID: %02x", msg->id);
    printf(". FIELDS: [ QR: %u, OpCode: %u ]", msg->qr, msg->opcode);
    printf(", QDcount: %u", msg->qdCount);
    printf(", ANcount: %u", msg->anCount);
    printf(", NScount: %u", msg->nsCount);
    printf(", ARcount: %u,\n", msg->arCount);

    q = msg->questions;
    while(q) {
        printf("    Question { qName '%s', qType %u, qClass %u }\n",
            q->qName,
            q->qType,
            q->qClass
        );
        q = q->next;
    }

    print_resource_record(msg->answers);
    print_resource_record(msg->authorities);
    print_resource_record(msg->additionals);

    printf("}\n");
}

size_t get16bits(const uint8_t **buffer) {
    uint16_t value;

    memcpy(&value, *buffer, 2);
    *buffer += 2;

    return ntohs(value);
}

void put8bits(uint8_t **buffer, uint8_t value) {
    memcpy(*buffer, &value, 1);
    *buffer += 1;
}

void put16bits(uint8_t **buffer, uint16_t value) {
    value = htons(value);
    memcpy(*buffer, &value, 2);
    *buffer += 2;
}

void put32bits(uint8_t **buffer, uint32_t value) {
    value = htonl(value);
    memcpy(*buffer, &value, 4);
    *buffer += 4;
}

// 3foo3bar3com0 => foo.bar.com (No full validation is done!)
char *decode_domain_name(const uint8_t **buf, size_t len) {
    char domain[256];
    for(int i = 1; i < MIN(256, len); i += 1) {
        uint8_t c = (*buf)[i];
        if(c == 0) {
            domain[i - 1] = 0;
            *buf += i + 1;
            return strdup(domain);
        } else if((c >= 'a' && c <= 'z') || c == '-' || (c >= '0' && c <= '9')) {
            domain[i - 1] = c;
        } else {
            domain[i - 1] = '.';
        }
    }

    return NULL;
}

// foo.bar.com => 3foo3bar3com0
void encode_domain_name(uint8_t **buffer, const char *domain) {
    uint8_t *buf = *buffer;
    const char *beg = domain;
    const char *pos;
    int len = 0;
    int i = 0;

    while((pos = strchr(beg, '.'))) {
        len = pos - beg;
        buf[i] = len;
        i += 1;
        memcpy(buf+i, beg, len);
        i += len;

        beg = pos + 1;
    }

    len = strlen(domain) - (beg - domain);

    buf[i] = len;
    i += 1;

    memcpy(buf + i, beg, len);
    i += len;

    buf[i] = 0;
    i += 1;

    *buffer += i;
}


void decode_header(struct Message *msg, const uint8_t **buffer) {
    msg->id = get16bits(buffer);

    uint32_t fields = get16bits(buffer);
    msg->qr = (fields & QR_MASK) >> 15;
    msg->opcode = (fields & OPCODE_MASK) >> 11;
    msg->aa = (fields & AA_MASK) >> 10;
    msg->tc = (fields & TC_MASK) >> 9;
    msg->rd = (fields & RD_MASK) >> 8;
    msg->ra = (fields & RA_MASK) >> 7;
    msg->rcode = (fields & RCODE_MASK) >> 0;

    msg->qdCount = get16bits(buffer);
    msg->anCount = get16bits(buffer);
    msg->nsCount = get16bits(buffer);
    msg->arCount = get16bits(buffer);
}

void encode_header(struct Message *msg, uint8_t **buffer) {
    put16bits(buffer, msg->id);

    int fields = 0;
    fields |= (msg->qr << 15) & QR_MASK;
    fields |= (msg->rcode << 0) & RCODE_MASK;
    put16bits(buffer, fields);
    put16bits(buffer, msg->qdCount);
    put16bits(buffer, msg->anCount);
    put16bits(buffer, msg->nsCount);
    put16bits(buffer, msg->arCount);
}

bool decode_msg(struct Message *msg, const uint8_t *buffer, size_t size) {
    int i;

    if(size < 12)
        return false;

    decode_header(msg, &buffer);

    if(msg->anCount != 0 || msg->nsCount != 0) {
        printf("Only questions expected!\n");
        return false;
    }

    uint32_t qcount = msg->qdCount;
    for(i = 0; i < qcount; i += 1) {
        struct Question *q = calloc(1, sizeof(struct Question));

        q->qName = decode_domain_name(&buffer, size);
        q->qType = get16bits(&buffer);
        q->qClass = get16bits(&buffer);

        if(q->qName == NULL) {
            printf("Failed to decode domain name!\n");
            return false;
        }

        q->next = msg->questions;
        msg->questions = q;
    }
    return true;
}

void resolve_query(struct Message *msg) {
    struct ResourceRecord *beg;
    struct ResourceRecord *rr;
    struct Question *q;

    msg->qr = 1;
    msg->aa = 1;
    msg->ra = 0;
    msg->rcode = Ok_ResponseType;
    msg->anCount = 0;
    msg->nsCount = 0;
    msg->arCount = 0;

    q = msg->questions;
    while(q) {
        rr = calloc(1, sizeof(struct ResourceRecord));

        rr->name = strdup(q->qName);
        rr->type = q->qType;
        rr->class = q->qClass;
        rr->ttl = 60*60;

        printf("Query for '%s'\n", q->qName);

        switch(q->qType) {
            case A_Resource_RecordType:
                rr->rd_length = 4;
                if(!get_A_Record(rr->rd_data.a_record.addr, q->qName)) {
                    free(rr->name);
                    free(rr);
                    goto next;
                }
                break;
            case AAAA_Resource_RecordType:
                rr->rd_length = 16;
                if(!get_AAAA_Record(rr->rd_data.aaaa_record.addr, q->qName)) {
                    free(rr->name);
                    free(rr);
                    goto next;
                }
                break;
            case TXT_Resource_RecordType:
                if(!get_TXT_Record(&(rr->rd_data.txt_record.txt_data), q->qName)) {
                    free(rr->name);
                    free(rr);
                    goto next;
                }
                int txt_data_len = strlen(rr->rd_data.txt_record.txt_data);
                rr->rd_length = txt_data_len + 1;
                rr->rd_data.txt_record.txt_data_len = txt_data_len;
                break;
            /*
            case NS_Resource_RecordType:
            case CNAME_Resource_RecordType:
            case SOA_Resource_RecordType:
            case PTR_Resource_RecordType:
            case MX_Resource_RecordType:
            case TXT_Resource_RecordType:
            */
            default:
                free(rr->name);
                free(rr);
                msg->rcode = NotImplemented_ResponseType;
                printf("Cannot answer question of type %d.\n", q->qType);
                goto next;
        }

        msg->anCount++;
        beg = msg->answers;
        msg->answers = rr;
        rr->next = beg;
        next:
        q = q->next;
    }
}

bool encode_resource_records(struct ResourceRecord *rr, uint8_t **buffer) {
    int i;

    while(rr) {
        encode_domain_name(buffer, rr->name);
        put16bits(buffer, rr->type);
        put16bits(buffer, rr->class);
        put32bits(buffer, rr->ttl);
        put16bits(buffer, rr->rd_length);

        switch(rr->type) {
            case A_Resource_RecordType:
                for(i = 0; i < 4; i += 1)
                    put8bits(buffer, rr->rd_data.a_record.addr[i]);
                break;
            case AAAA_Resource_RecordType:
                for(i = 0; i < 16; i += 1)
                    put8bits(buffer, rr->rd_data.aaaa_record.addr[i]);
                break;
            case TXT_Resource_RecordType:
                put8bits(buffer, rr->rd_data.txt_record.txt_data_len);
                for(i = 0; i < rr->rd_data.txt_record.txt_data_len; i++)
                    put8bits(buffer, rr->rd_data.txt_record.txt_data[i]);
                break;
            default:
                fprintf(stderr, "Unknown type %u. => Ignore resource record.\n", rr->type);
                return false;
        }

        rr = rr->next;
    }

    return true;
}

bool encode_msg(struct Message *msg, uint8_t **buffer) {
    encode_header(msg, buffer);

    struct Question *q = msg->questions;
    while(q) {
        encode_domain_name(buffer, q->qName);
        put16bits(buffer, q->qType);
        put16bits(buffer, q->qClass);

        q = q->next;
    }

    if(!encode_resource_records(msg->answers, buffer)) {
        return false;
    }

    if(!encode_resource_records(msg->authorities, buffer)) {
        return false;
    }

    if(!encode_resource_records(msg->additionals, buffer)) {
        return false;
    }

    return true;
}

void free_resource_records(struct ResourceRecord *rr) {
    struct ResourceRecord *next;

    while(rr) {
        free(rr->name);
        next = rr->next;
        free(rr);
        rr = next;
    }
}

void free_questions(struct Question *qq) {
    struct Question *next;

    while(qq) {
        free(qq->qName);
        next = qq->next;
        free(qq);
        qq = next;
    }
}

int main() {
    uint8_t buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    struct sockaddr_in addr;
    int rc;
    ssize_t nbytes;
    int sock;
    int port = 9000;

    struct Message msg;
    memset(&msg, 0, sizeof(struct Message));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    rc = bind(sock, (struct sockaddr*) &addr, addr_len);

    if(rc != 0) {
        printf("Could not bind: %s\n", strerror(errno));
        return 1;
    }

    printf("Listening on port %u.\n", port);

    while(1) {
        free_questions(msg.questions);
        free_resource_records(msg.answers);
        free_resource_records(msg.authorities);
        free_resource_records(msg.additionals);
        memset(&msg, 0, sizeof(struct Message));
        nbytes = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *) &client_addr, &addr_len);

        if(nbytes < 0) {
            continue;
        }

        if(!decode_msg(&msg, buffer, nbytes)) {
            continue;
        }

        print_message(&msg);

        resolve_query(&msg);

        print_message(&msg);

        uint8_t *p = buffer;
        if(!encode_msg(&msg, &p)) {
            continue;
        }

        size_t buflen = p - buffer;
        sendto(sock, buffer, buflen, 0, (struct sockaddr*) &client_addr, addr_len);
    }
}
