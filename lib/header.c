/* RPM - Copyright (C) 1995 Red Hat Software
 *
 * header.c - routines for managing rpm headers
 */

/* Data written to file descriptors is in network byte order.    */
/* Data read from file descriptors is expected to be in          */
/* network byte order and is converted on the fly to host order. */

#include <stdlib.h>
#include <ctype.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>

#include "header.h"
#include "rpmlib.h"		/* necessary only for dumpHeader() */
#include "tread.h"

#define INDEX_MALLOC_SIZE 8
#define DATA_MALLOC_SIZE 1024

static unsigned char header_magic[4] = { 0x8e, 0xad, 0xe8, 0x01 };

struct headerToken {
    struct indexEntry *index;
    int entries_malloced;
    int entries_used;

    char *data;
    int data_malloced;
    int data_used;

    int fully_sorted;  /* This means the index and the data! */
};

struct indexEntry {
    int_32 tag;
    int_32 type;
    int_32 offset;		/* Offset from beginning of data segment */
    int_32 count;
};

static int indexSort(const void *ap, const void *bp);
static struct indexEntry *findEntry(Header h, int_32 tag);
static void *dataHostToNetwork(Header h);
static void *dataNetworkToHost(Header h);

/********************************************************************/
/*                                                                  */
/* Header iteration and copying                                     */
/*                                                                  */
/********************************************************************/

struct headerIteratorS {
    Header h;
    int next_index;
};

HeaderIterator initIterator(Header h)
{
    HeaderIterator hi = malloc(sizeof(struct headerIteratorS));
    hi->h = h;
    hi->next_index = 0;
    return hi;
}

void freeIterator(HeaderIterator iter)
{
    free(iter);
}

int nextIterator(HeaderIterator iter,
		 int_32 *tag, int_32 *type, void **p, int_32 *c)
{
    struct headerToken *h = iter->h;
    struct indexEntry *index = h->index;
    int x = h->entries_used;
    int slot = iter->next_index;
    char **spp;
    char *sp;

    if (slot == h->entries_used) {
	return 0;
    }
    iter->next_index++;
    
    *tag = index[slot].tag;
    *type = index[slot].type;
    *c = index[slot].count;

    /* Now look it up */
    switch (*type) {
    case INT64_TYPE:
    case INT32_TYPE:
    case INT16_TYPE:
    case INT8_TYPE:
    case BIN_TYPE:
    case CHAR_TYPE:
	*p = h->data + index[slot].offset;
	break;
    case STRING_TYPE:
	if (*c == 1) {
	    /* Special case -- just return a pointer to the string */
	    *p = h->data + index[slot].offset;
	    break;
	}
	/* Fall through to STRING_ARRAY_TYPE */
    case STRING_ARRAY_TYPE:
        /* Correction! */
        *type = STRING_ARRAY_TYPE;
	/* Otherwise, build up an array of char* to return */
	x = index[slot].count;
	*p = malloc(x * sizeof(char *));
	spp = (char **) *p;
	sp = h->data + index[slot].offset;
	while (x--) {
	    *spp++ = sp;
	    sp = strchr(sp, 0);
	    sp++;
	}
	break;
    default:
	fprintf(stderr, "Data type %d not supported\n", (int) *type);
	exit(1);
    }

    return 1;
}

static int indexSort(const void *ap, const void *bp)
{
    int_32 a, b;

    a = ((struct indexEntry *)ap)->tag;
    b = ((struct indexEntry *)bp)->tag;
    
    if (a > b) {
	return 1;
    } else if (a < b) {
	return -1;
    } else {
	return 0;
    }
}

Header copyHeader(Header h)
{
    int_32 tag, type, count;
    void *ptr;
    HeaderIterator headerIter;
    Header res = newHeader();
   
    /* Sort the index */
    qsort(h->index, h->entries_used, sizeof(struct indexEntry), indexSort);
    headerIter = initIterator(h);

    /* The result here is that the data is also sorted */
    while (nextIterator(headerIter, &tag, &type, &ptr, &count)) {
	addEntry(res, tag, type, ptr, count);
    }

    res->fully_sorted = 1;
    return res;
}

/********************************************************************/
/*                                                                  */
/* Reading and writing headers                                      */
/*                                                                  */
/********************************************************************/

void writeHeader(int fd, Header h, int magicp)
{
    int_32 l;
    struct indexEntry *p;
    struct indexEntry *copyIndex;
    int c;
    void *converted_data;

    /* This magic actually sorts the data */
    h = copyHeader(h);

    /* We must write using network byte order! */

    /* Magic and 4 reserved bytes */
    if (magicp) {
	write(fd, header_magic, sizeof(header_magic));
	l = htonl(0);
	write(fd, &l, sizeof(l));
    }
    
    /* First write out the length of the index (count of index entries) */
    l = htonl(h->entries_used);
    write(fd, &l, sizeof(l));

    /* Then write the length of the data (number of bytes) */
    l = htonl(h->data_used);
    write(fd, &l, sizeof(l));

    /* Make a copy of the index for htonl()  */
    copyIndex = malloc(sizeof(struct indexEntry) * h->entries_used);
    memcpy(copyIndex, h->index, sizeof(struct indexEntry) * h->entries_used);

    /* Convert the index */
    c = h->entries_used;
    p = copyIndex;
    while (c--) {
	p->tag = htonl(p->tag);
	p->type = htonl(p->type);
	p->offset = htonl(p->offset);
	p->count = htonl(p->count);
	p++;
    }

    /* Write the index */
    write(fd, copyIndex, sizeof(struct indexEntry) * h->entries_used);
    free(copyIndex);

    /* Finally convert and write the data */
    converted_data = dataHostToNetwork(h);
    write(fd, converted_data, h->data_used);
    free(converted_data);

    free(h);
}

static void *dataHostToNetwork(Header h)
{
    char *data, *p;
    struct indexEntry *index = h->index;
    int entries = h->entries_used;
    int count;

    data = malloc(h->data_used);
    memcpy(data, h->data, h->data_used);

    while (entries--) {
	p = data + index->offset;
	count = index->count;
	switch (index->type) {
	case INT64_TYPE:
	    while (count--) {
		*((int_64 *)p) = htonl(*((int_64 *)p));
		p += sizeof(int_64);
	    }
	    break;
	case INT32_TYPE:
	    while (count--) {
		*((int_32 *)p) = htonl(*((int_32 *)p));
		p += sizeof(int_32);
	    }
	    break;
	case INT16_TYPE:
	    while (count--) {
		*((int_16 *)p) = htons(*((int_16 *)p));
		p += sizeof(int_16);
	    }
	    break;
	case INT8_TYPE:
	case BIN_TYPE:
	case CHAR_TYPE:
	case STRING_TYPE:
	case STRING_ARRAY_TYPE:
	    /* No conversion necessary */
	    break;
	default:
	    fprintf(stderr, "Data type %d not supprted\n", (int) index->type);
	    exit(1);
	}
	
	index++;
    }
    
    return data;
}

Header readHeader(int fd, int magicp)
{
    int_32 il, dl;
    unsigned char magic[4];
    int_32 reserved;
    struct indexEntry *p;
    int c;
    void *converted_data;

    struct headerToken *h = (struct headerToken *)
    malloc(sizeof(struct headerToken));

    if (magicp == HEADER_MAGIC) {
	c = timedRead(fd, magic, sizeof(magic));
	message(MESS_DEBUG, "magic: %02x %02x %02x %02x\n",
		header_magic[0],
		header_magic[1],
		header_magic[2],
		header_magic[3]);
	message(MESS_DEBUG, "got  : %02x %02x %02x %02x\n",
		magic[0],
		magic[1],
		magic[2],
		magic[3]);
	if (memcmp(magic, header_magic, sizeof(magic))) {
	    free(h);
	    return NULL;
	}
	timedRead(fd, &reserved, sizeof(reserved));
	reserved = ntohl(reserved);
    }
    
    /* First read the index length (count of index entries) */
    if (timedRead(fd, &il, sizeof(il)) != sizeof(il)) {
	free(h);
	return NULL;
    }
    il = ntohl(il);

    /* Then read the data length (number of bytes) */
    if (timedRead(fd, &dl, sizeof(dl)) != sizeof(dl)) {
	free(h);
	return NULL;
    }
    dl = ntohl(dl);

    /* Next read the index */
    h->index = malloc(il * sizeof(struct indexEntry));
    h->entries_malloced = il;
    h->entries_used = il;
    if (timedRead(fd, h->index, sizeof(struct indexEntry) * il) !=
	sizeof(struct indexEntry) * il) {
	if (h->index)
	    free(h->index);
	free(h);
	return NULL;
    }

    /* Convert the index */
    c = h->entries_used;
    p = h->index;
    while (c--) {
	p->tag = ntohl(p->tag);
	p->type = ntohl(p->type);
	p->offset = ntohl(p->offset);
	p->count = ntohl(p->count);
	p++;
    }

    /* Finally, read the data */
    h->data = malloc(dl);
    h->data_malloced = dl;
    h->data_used = dl;
    if (timedRead(fd, h->data, dl) != dl) {
	if (h->data)
	    free(h->data);
	if (h->index)
	    free(h->index);
	free(h);
	return NULL;
    }
    /* and convert it */
    converted_data = dataNetworkToHost(h);
    free(h->data);
    h->data = converted_data;

    h->fully_sorted = 1;

    return h;
}

static void *dataNetworkToHost(Header h)
{
    char *data, *p;
    struct indexEntry *index = h->index;
    int entries = h->entries_used;
    int count;

    data = malloc(h->data_used);
    memcpy(data, h->data, h->data_used);

    while (entries--) {
	p = data + index->offset;
	count = index->count;
	switch (index->type) {
	case INT64_TYPE:
	    while (count--) {
		*((int_64 *)p) = ntohl(*((int_64 *)p));
		p += sizeof(int_64);
	    }
	    break;
	case INT32_TYPE:
	    while (count--) {
		*((int_32 *)p) = ntohl(*((int_32 *)p));
		p += sizeof(int_32);
	    }
	    break;
	case INT16_TYPE:
	    while (count--) {
		*((int_16 *)p) = ntohs(*((int_16 *)p));
		p += sizeof(int_16);
	    }
	    break;
	case INT8_TYPE:
	case BIN_TYPE:
	case CHAR_TYPE:
	case STRING_TYPE:
	case STRING_ARRAY_TYPE:
	    /* No conversion necessary */
	    break;
	default:
	    fprintf(stderr, "Data type %d not supprted\n", (int) index->type);
	    exit(1);
	}
	
	index++;
    }
    
    return data;
}

/********************************************************************/
/*                                                                  */
/* Header loading and unloading                                     */
/*                                                                  */
/********************************************************************/

Header loadHeader(void *pv)
{
    int_32 il, dl;		/* index length, data length */
    char *p = pv;
    struct headerToken *h = malloc(sizeof(struct headerToken));

    il = *((int_32 *) p);
    p += sizeof(int_32);
    dl = *((int_32 *) p);
    p += sizeof(int_32);

    h->entries_malloced = il;
    h->entries_used = il;
    h->index = malloc(il * sizeof(struct indexEntry));
    memcpy(h->index, p, il * sizeof(struct indexEntry));
    p += il * sizeof(struct indexEntry);

    h->data_malloced = dl;
    h->data_used = dl;
    h->data = malloc(dl);
    memcpy(h->data, p, dl);

    /* This assumes you only loadHeader() something you unloadHeader()-ed */
    h->fully_sorted = 1;

    return h;
}

void *unloadHeader(Header h)
{
    void *p;
    int_32 *pi;
    char * chptr;

    /* This magic actually sorts the data */
    h = copyHeader(h);

    pi = p = malloc(2 * sizeof(int_32) +
		    h->entries_used * sizeof(struct indexEntry) +
		    h->data_used);

    *pi++ = h->entries_used;
    *pi++ = h->data_used;

    chptr = (char *) pi;

    memcpy(chptr, h->index, h->entries_used * sizeof(struct indexEntry));
    chptr += h->entries_used * sizeof(struct indexEntry);
    memcpy(chptr, h->data, h->data_used);

    freeHeader(h);
   
    return p;
}

/********************************************************************/
/*                                                                  */
/* Header dumping                                                   */
/*                                                                  */
/********************************************************************/

void dumpHeader(Header h, FILE * f, int flags)
{
    int i, c, ct;
    struct indexEntry *p;
    char *dp;
    char ch;
    char *type, *tag;

    /* First write out the length of the index (count of index entries) */
    fprintf(f, "Entry count: %d\n", h->entries_used);

    /* And the length of the data (number of bytes) */
    fprintf(f, "Data count : %d\n", h->data_used);

    /* Now write the index */
    p = h->index;
    fprintf(f, "\n             CT  TAG                  TYPE         "
		"      OFSET      COUNT\n");
    for (i = 0; i < h->entries_used; i++) {
	switch (p->type) {
	    case NULL_TYPE:   		type = "NULL_TYPE"; 	break;
	    case CHAR_TYPE:   		type = "CHAR_TYPE"; 	break;
	    case BIN_TYPE:   		type = "BIN_TYPE"; 	break;
	    case INT8_TYPE:   		type = "INT8_TYPE"; 	break;
	    case INT16_TYPE:  		type = "INT16_TYPE"; 	break;
	    case INT32_TYPE:  		type = "INT32_TYPE"; 	break;
	    case INT64_TYPE:  		type = "INT64_TYPE"; 	break;
	    case STRING_TYPE: 	    	type = "STRING_TYPE"; 	break;
	    case STRING_ARRAY_TYPE: 	type = "STRING_ARRAY_TYPE"; break;
	    default:		    	type = "(unknown)";	break;
	}

	tag = "(unknown)";
	c = 0;
	while (c < rpmTagTableSize) {
	    if (rpmTagTable[c].val == p->tag) {
		tag = rpmTagTable[c].name + 7;
	    }
	    c++;
	}

	fprintf(f, "Entry      : %.3d (%d)%-14s %-18s 0x%.8x %.8d\n", i,
		p->tag, tag, type, (uint_32) p->offset, (uint_32) p->count);

	if (flags & DUMP_INLINE) {
	    /* Print the data inline */
	    dp = h->data + p->offset;
	    c = p->count;
	    ct = 0;
	    switch (p->type) {
	    case INT32_TYPE:
		while (c--) {
		    fprintf(f, "       Data: %.3d 0x%08x (%d)\n", ct++,
			    (uint_32) *((int_32 *) dp),
			    (uint_32) *((int_32 *) dp));
		    dp += sizeof(int_32);
		}
		break;

	    case INT16_TYPE:
		while (c--) {
		    fprintf(f, "       Data: %.3d 0x%04x (%d)\n", ct++,
			    (short int) *((int_16 *) dp),
			    (short int) *((int_16 *) dp));
		    dp += sizeof(int_16);
		}
		break;
	    case INT8_TYPE:
		while (c--) {
		    fprintf(f, "       Data: %.3d 0x%02x (%d)\n", ct++,
			    (char) *((int_8 *) dp),
			    (char) *((int_8 *) dp));
		    dp += sizeof(int_8);
		}
		break;
	    case BIN_TYPE:
	      while (c > 0) {
		  fprintf(f, "       Data: %.3d ", ct);
		  while (c--) {
		      fprintf(f, "%02x ", (unsigned char) *(int_8 *)dp);
		      ct++;
		      dp += sizeof(int_8);
		      if (! (ct % 8)) {
			  break;
		      }
		  }
		  fprintf(f, "\n");
	      }
		break;
	    case CHAR_TYPE:
		while (c--) {
		    ch = (char) *((char *) dp);
		    fprintf(f, "       Data: %.3d 0x%2x %c (%d)\n", ct++,
			    ch,
			    (isprint(ch) ? ch : ' '),
			    (char) *((char *) dp));
		    dp += sizeof(char);
		}
		break;
	    case STRING_TYPE:
	    case STRING_ARRAY_TYPE:
		while (c--) {
		    fprintf(f, "       Data: %.3d %s\n", ct++, (char *) dp);
		    dp = strchr(dp, 0);
		    dp++;
		}
		break;
	    default:
		fprintf(stderr, "Data type %d not supprted\n", (int) p->type);
		exit(1);
	    }
	}
	p++;
    }
}

/********************************************************************/
/*                                                                  */
/* Entry lookup                                                     */
/*                                                                  */
/********************************************************************/

static int tagCompare(const void *key, const void *member)
{
    if (*((int_32 *)key) > ((struct indexEntry *)member)->tag) {
	return 1;
    } else if (*((int_32 *)key) < ((struct indexEntry *)member)->tag) {
	return -1;
    } else {
	return 0;
    }
}

static struct indexEntry *findEntry(Header h, int_32 tag)
{
    struct indexEntry *index = h->index;
    int x = h->entries_used;

    if (h->fully_sorted) {
	return bsearch(&tag, index, x, sizeof(struct indexEntry), tagCompare);
    } else {
	while (x && (tag != index->tag)) {
	    index++;
	    x--;
	}
	return (x ? index : NULL);
    }
}

int isEntry(Header h, int_32 tag)
{
    return (findEntry(h, tag) ? 1 : 0);
}

int getEntry(Header h, int_32 tag, int_32 * type, void **p, int_32 * c)
{
    struct indexEntry *index;
    char **spp;
    char *sp;
    int x;

    if (!p) return isEntry(h, tag);

    /* First find the tag */
    index = findEntry(h, tag);
    if (! index) {
	*p = NULL;
	return 0;
    }

    if (type) {
	*type = (int) index->type;
    }
    if (c) {
	*c = index->count;
    }

    /* Now look it up */
    switch (index->type) {
    case INT64_TYPE:
    case INT32_TYPE:
    case INT16_TYPE:
    case INT8_TYPE:
    case BIN_TYPE:
    case CHAR_TYPE:
	*p = h->data + index->offset;
	break;
    case STRING_TYPE:
	if (index->count == 1) {
	    /* Special case -- just return a pointer to the string */
	    *p = h->data + index->offset;
	    break;
	}
	/* Fall through to STRING_ARRAY_TYPE */
    case STRING_ARRAY_TYPE:
        /* Correction! */
        if (type) {
	    *type = STRING_ARRAY_TYPE;
	}
	/* Otherwise, build up an array of char* to return */
	x = index->count;
	*p = malloc(x * sizeof(char *));
	spp = (char **) *p;
	sp = h->data + index->offset;
	while (x--) {
	    *spp++ = sp;
	    sp = strchr(sp, 0);
	    sp++;
	}
	break;
    default:
	fprintf(stderr, "Data type %d not supprted\n",
		(int) index->type);
	exit(1);
    }

    return 1;
}

/********************************************************************/
/*                                                                  */
/* Header creation and deletion                                     */
/*                                                                  */
/********************************************************************/

Header newHeader()
{
    struct headerToken *h = (struct headerToken *)
    malloc(sizeof(struct headerToken));

    h->data = malloc(DATA_MALLOC_SIZE);
    h->data_malloced = DATA_MALLOC_SIZE;
    h->data_used = 0;

    h->index = malloc(INDEX_MALLOC_SIZE * sizeof(struct indexEntry));
    h->entries_malloced = INDEX_MALLOC_SIZE;
    h->entries_used = 0;

    h->fully_sorted = 0;

    return (Header) h;
}

void freeHeader(Header h)
{
    free(h->index);
    free(h->data);
    free(h);
}

unsigned int sizeofHeader(Header h, int magicp)
{
    unsigned int size;

    /* Do some real magic to determine the ON-DISK size */
    h = copyHeader(h);
   
    size = sizeof(int_32);	/* count of index entries */
    size += sizeof(int_32);	/* length of data */
    size += sizeof(struct indexEntry) * h->entries_used;
    size += h->data_used;
    if (magicp) {
	size += 8;
    }

    freeHeader(h);
   
    return size;
}

/********************************************************************/
/*                                                                  */
/* Adding and modifying entries                                     */
/*                                                                  */
/********************************************************************/

int addEntry(Header h, int_32 tag, int_32 type, void *p, int_32 c)
{
    struct indexEntry *entry;
    void *ptr;
    char **spp;
    char *sp;
    int i, length;
    int pad;

    if (c <= 0) {
	fprintf(stderr, "Bad count for addEntry(): %d\n", (int) c);
	exit(1);
    }

    /* Allocate more index space if necessary */
    if (h->entries_used == h->entries_malloced) {
	h->entries_malloced += INDEX_MALLOC_SIZE;
	h->index = realloc(h->index,
			h->entries_malloced * sizeof(struct indexEntry));
    }

    /* Fill in the index */
    i = h->entries_used++;
    entry = &((h->index)[i]);
    entry->tag = tag;
    entry->type = type;
    entry->count = c;

    /* Compute length of data to add */
    pad = 0;
    switch (type) {
    case INT64_TYPE:
	length = sizeof(int_64) * c;
	pad = 8;
	break;
    case INT32_TYPE:
	length = sizeof(int_32) * c;
	pad = 4;
	break;
    case INT16_TYPE:
	length = sizeof(int_16) * c;
	pad = 2;
	break;
    case INT8_TYPE:
	length = sizeof(int_8) * c;
	break;
    case BIN_TYPE:
    case CHAR_TYPE:
	length = sizeof(char) * c;
	break;
    case STRING_TYPE:
	if (c == 1) {
	    /* Special case -- p is just the string */
	    length = strlen(p) + 1;
	    break;
	}
	/* Otherwise fall through to STRING_ARRAY_TYPE */
        /* This should not be allowed */
	fprintf(stderr, "addEntry() STRING_TYPE count must be 1.\n");
	exit(1);
    case STRING_ARRAY_TYPE:
	/* This is like STRING_TYPE, except it's *always* an array */
	/* Compute sum of length of all strings, including null terminators */
	i = c;
	spp = p;
	length = 0;
	while (i--) {
	    /* add one for null termination */
	    length += strlen(*spp++) + 1;
	}
	break;
    default:
	fprintf(stderr, "Data type %d not supprted\n", (int) type);
	exit(1);
    }

    if (pad) {
	pad = (pad - (h->data_used % pad)) % pad;
    }
    
    /* Allocate more data space if necessary */
    while ((length + pad + h->data_used) > h->data_malloced) {
	h->data_malloced += DATA_MALLOC_SIZE;
	h->data = realloc(h->data, h->data_malloced);
    }

    /* Fill in the data */
    entry->offset = h->data_used + pad;
    ptr = h->data + h->data_used + pad;
    switch (type) {
      case INT32_TYPE:
      case INT16_TYPE:
      case INT8_TYPE:
      case BIN_TYPE:
      case CHAR_TYPE:
	memcpy(ptr, p, length);
	break;
      case STRING_TYPE:
	if (c == 1) {
	    /* Special case -- p is just the string */
	    strcpy(ptr, p);
	    break;
	}
	/* Fall through to STRING_ARRAY_TYPE */
        /* This should not be allowed */
	fprintf(stderr, "addEntry() internal error!.\n");
	exit(1);
      case STRING_ARRAY_TYPE:
	  /* Otherwise, p is char** */
	  i = c;
	  spp = p;
	  sp = (char *) ptr;
	  while (i--) {
	      strcpy(sp, *spp);
	      sp += strlen(*spp++) + 1;
	  }
	break;
      default:
	fprintf(stderr, "Data type %d not supprted\n", (int) type);
	exit(1);
    }

    h->data_used += length + pad;
    h->fully_sorted = 0;

    return 1;
}

int modifyEntry(Header h, int_32 tag, int_32 type, void *p, int_32 c)
{
    struct indexEntry *index;

    /* First find the tag */
    index = findEntry(h, tag);
    if (! index) {
	return 0;
    }

    if (type != index->type) {
	return 0;
    }

    if (c != 1) {
	return 0;
    }

    if (index->count != 1) {
	return 0;
    }
    
    switch (index->type) {
    case INT64_TYPE:
	*((int_64 *)(h->data + index->offset)) = *((int_64 *)p);
	break;
    case INT32_TYPE:
	*((int_32 *)(h->data + index->offset)) = *((int_32 *)p);
	break;
    case INT16_TYPE:
	*((int_16 *)(h->data + index->offset)) = *((int_16 *)p);
	break;
    case INT8_TYPE:
	*((int_8 *)(h->data + index->offset)) = *((int_8 *)p);
	break;
    case BIN_TYPE:
    case CHAR_TYPE:
	*((char *)(h->data + index->offset)) = *((char *)p);
	break;
    default:
	return 0;
    }

    return 1;
}
