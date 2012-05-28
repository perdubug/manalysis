/*
 * ma(memory analyzer) is a Post Analysis command-line utility programi that allows you to know memory usage(heap and block) for S40 
 *
 *
 * History:
 *     20/05/2012   Yang Ming  Init version.
 *
 * How to build it?
 *     - Linux(Default)
 *          gcc -lpthread -lm -Wall -o example example.c 
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <pthread.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mman.h>

/************************************************************************** 
   Data types...
 **************************************************************************/
typedef unsigned char      uint8;
typedef short              uint16;
typedef unsigned int       uint32;
typedef unsigned long long uint64;
typedef signed int         sint32;

/************************************************************************** 
   Macros...
 **************************************************************************/
/* TODO:use the real one */
#define TOTAL_FREE_HEAP_BYTES (6.6*1024*1024)

#define TEMPLATE_FILE_LIST         "./meta_tmp/tmp_blx_file_list"
#define META_FILE_LIST             "./meta_tmp/meta_file_list"
#define TRACE_START_DATE           "./meta_tmp/file_date"
#define DEFAULT_META_FILE          "./meta_tmp/meta.csv"
#define REMOVE_DEFAULT_META_FILE   "rm -rf ./meta_tmp/meta.csv"
#define REMOVE_DEFAULT_META_FOLDER "rm -rf ./meta_tmp"
#define CREATE_DEFAULT_META_FOLDER "mkdir ./meta_tmp"
#define DEFAULT_CSV_FILE_PREFIX    "csv_samplerate_"
#define DEFAULT_META_FILE_SUFFRIX  ".meta"
#define DEFAULT_META_FOLDER_PREFIX "./meta_tmp/"

/** meta file format
    -----------------------------------------------------------------------------------
    Timestamp | Operation Type | Address | Size | Allocation Type | Caller1 | Caller2 |
    -----------------------------------------------------------------------------------

    Timestamp:        HH:MM:SS.000000000
    Operation Type:   + means deallocation, - means allocation
    Size:             Only available for allocation
    Allocation Type:  See ALLOCATION_TYPE
    Caller and caller2 

 */
#define META_DATA_FORMAT "%-19.19s %c %8x%8d%2d %8x %8x\n"  /* use for sprintf */

#define TRUE   1
#define FALSE  0

#define GET_SIZE(size_arr) (size_arr[0]<<24 | size_arr[1]<<16 | size_arr[2]<<8 | size_arr[3])
#define GET_PTR GET_SIZE

#ifndef FILESYSTEM_PREFIX_LEN
# define FILESYSTEM_PREFIX_LEN(Filename) 0
#endif

#ifndef ISSLASH
# define ISSLASH(C) ((C) == '/')
#endif

#define SECONDS_FOR_ONE_DAY (60 * 60 * 24)
#define TIME_UNIT 1000000000LL
#define INVALID_HOUR 0xFF
#define IS_A_NUMBER(x) ((x)>='0' && (x)<='9')

#define TYPE_ALLOCATE   '-'
#define TYPE_DEALLOCATE '+'

#define MAX_PATH_LEN             512
#define MAX_INDEX_LEN            5
#define MAX_SINGLE_METADATA_LEN  128
#define MAX_NUM_THREADS          5
#define MAX_THEORY_HEAP_SIZE     0xFFFFFFFF

#define ENABLE_TRACE_GENERAL     FALSE
#define ENABLE_TRACE_INFO        FALSE
#define ENABLE_DEBUG_INFO        FALSE
#define ENABLE_LINKED_LIST_TRACE FALSE
#define ENABLE_HEAP_TRACE        FALSE

#define MUTEX_LOCK(r)   pthread_mutex_lock(&r)
#define MUTEX_UNLOCK(r) pthread_mutex_unlock(&r)

#define SET_DEFAULT_START_DATE(date)  {date.day = 3;date.month = 3;date.year = 2012;}
/**
 * Signature bytes used when searching for heap trace messages
 */
#define SIGNATURE_MESSAGE_ID              0x94  /* message id is TB_TRACE_MSG */
#define SIGNATURE_MASTER                  0x01  /* master is MASTER_01_VENDOR */
#define SIGNATURE_HEAP_TYPE               0x04
#define SIGNATURE_HEAP_ALLOC              0x40
#define SIGNATURE_HEAP_DEALLOC            0x42
#define SIGNATURE_HEAP_ALLOC_NO_WAIT      0x76
#define SIGNATURE_HEAP_COND_ALLOC         0x77
#define SIGNATURE_HEAP_ALLOC_NO_WAIT_FROM 0x84  /* HOOK_HEAP_ALLOC_NO_WAIT_FROM     */
#define SIGNATURE_ALIGNED_ALLOC_NO_WAIT   0x8A  /* HOOK_ALIGNED_BLOCK_ALLOC_NO_WAIT */
#define SIGNATURE_ALIGNED_ALLOC           0x8B  /* HOOK_ALIGNED_BLOCK_ALLOC         */


/************************************************************************** 
   structs...
 **************************************************************************/
enum IDX{
   IDX_HEAP_ALLOC,
   IDX_HEAP_DEALLOC,
   IDX_HEAP_ALLOC_NO_WAIT,
   IDX_HEAP_COND_ALLOC,
   IDX_ALIGNED_ALLOC_NO_WAIT,
   IDX_ALIGNED_ALLOC,
   IDX_HEAP_ALLOC_NO_WAIT_FROM,
   IDX_NUMS
};

/*TODO: That's all? Is that enough for all allocation and deallocation types? */
enum ALLOCATION_TYPE{
   AT_HEAP_ALLOC = 1,
   AT_HEAP_ALLOC_NO_WAIT,
   AT_HEAP_COND_ALLOC,
   AT_ALIGNED_ALLOC_NO_WAIT,
   AT_ALIGNED_ALLOC,
   AT_ALLOC_NO_WAIT_FROM,
   AT_NUMS
};

typedef struct TRACE_DATE {
    uint16  day;
    uint16  month;
    uint16  year;
} TRACE_DATE;

typedef struct HEAP_DEALLOC_TAIL {
    uint8 caller1[4];
    uint8 caller2[4];
} HEAP_DEALLOC_TAIL;

typedef struct HEAP_ALLOC_TAIL {
    uint8 size[4];
    uint8 caller1[4];
    uint8 caller2[4];
} HEAP_ALLOC_TAIL;

typedef struct HEAP_ALLOC_NWF_TAIL {
    uint8 size[4];
    uint8 heapid[2];
    uint8 caller1[4];
    uint8 caller2[4];
} HEAP_ALLOC_NWF_TAIL;

typedef struct HEAP_COND_ALLOC_TAIL {
    uint8 size[4];
    uint8 low_water_mark[4];
    uint8 caller1[4];
    uint8 caller2[4];
} HEAP_COND_ALLOC_TAIL;

typedef struct HEAP_ALIGNED_ANW_TAIL {
    uint8 size[4];
    uint8 block_alignment[4];
    uint8 caller1[4];
    uint8 caller2[4];
} HEAP_ALIGNED_ANW_TAIL;

typedef struct HEAP_TRACE_INFO {
    uint8  channel;
    uint8  time[8];
    uint8  type;
    uint8  id;
    uint8  task[2];
    uint8  ptr[4];

    /* difference OS heap trace has difference tail... */ 
    union {
      HEAP_ALLOC_TAIL         hat;
      HEAP_DEALLOC_TAIL       hdt;
      HEAP_COND_ALLOC_TAIL    hcat;
      HEAP_ALIGNED_ANW_TAIL   haat;  
      HEAP_ALLOC_NWF_TAIL     hanwft;
    };

} HEAP_TRACE_INFO;

typedef struct HEAP_LINK_NODE {
    uint32 addr;
    uint32 size;

    struct HEAP_LINK_NODE * next;
} HEAP_LINK_NODE;

/* below data struct are for sort file list
 */
typedef struct BLX_FILE_IN_ONE_FOLDER {
    uint16 index;
    uint8  filepath[MAX_PATH_LEN];

    struct BLX_FILE_IN_ONE_FOLDER * next;
} BLX_FILE_IN_ONE_FOLDER;

typedef struct BLX_FILE_LIST_NODE {
    uint8  pathpattern[MAX_PATH_LEN]; /* for identify if two blx files are in same foler */
    struct BLX_FILE_IN_ONE_FOLDER * first_node;
    struct BLX_FILE_LIST_NODE     * next_list;
} BLX_FILE_LIST_NODE;

typedef struct THREAD_PARAMETER {
    uint32    fileindex;               /* use to make all blx files name unique     */   
    uint16    threadsid;               /* thread index                              */
    char      filepath[MAX_PATH_LEN];  /* which blx file the thread needs to decode */ 
}THREAD_PARAMETER;

typedef struct THREAD_UNIT {    
    pthread_t pt;        /* thread pointer                                            */  
    uint16    busy;      /* TRUE - the thread is working on decoing, FALSE- it's free */
} THREAD_UNIT;

typedef struct META_FORMAT_TIME {
    char hour[2];
    char skip1; 
    char minute[2];
    char skip2;
    char second[2];
    char skip3;
    char ms[9];
} META_FORMAT_TIME;

/* for .meta file */
typedef struct META_FORMAT_UNIT {
    META_FORMAT_TIME mft;
	char skip[2];    
    char type;      /* + means deallocation, - mean allocation */
    char skip2;
    char address[8];
    char size[8];   /* for deallocation it is zero */
    char skip3;
    char allocation_type; /* see ALLOCATION_TYPE  */
    char skip4;
    char caller1[8];
    char skip5;
    char caller2[8];
    char skip6;
} META_FORMAT_UNIT;

typedef struct  META_DATE{
    char day[2];
    char skip1;
    char month[2];
    char skip2;
    char year[4];
    char skip3;
    char hour[2];
    char skip4;
    char minute[2];
    char skip5;	
    char second[2];
    char skip6;
    char ms[9];
}META_DATE;

/* for .csv file */
typedef struct CSV_FORMAT_UNIT {
    META_DATE md;
	char skip;
	char freesize[8]; 
} CSV_FORMAT_UNIT;

typedef struct  TRACE_TIME{
    uint16 minute;
    uint16 hour;
    uint16 day;    
}TRACE_TIME;

/************************************************************************** 
   global variables...
 **************************************************************************/
TRACE_DATE           g_trace_date;
BLX_FILE_LIST_NODE * g_bfln_header;
uint32               g_free_heap = TOTAL_FREE_HEAP_BYTES;
HEAP_LINK_NODE     * g_heap_link;

THREAD_UNIT          g_thread_pool[MAX_NUM_THREADS];
pthread_mutex_t      g_thread_flag_mutex;   /* used to protect g_thread_pool */

/************************************************************************** 
    functions...
 **************************************************************************/
uint32 char_2_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 255;
}

uint32 strtouint32(char * in)
{
    char * buf = 0;
    char * endptr = NULL; 
    uint64 ret;

    buf = in;
         
    /* To distinguish success/failure after call */
    errno = 0;
    ret = strtoll(buf, &endptr, 10);

    /* Check for various possible errors */
    if ( errno == ERANGE || (errno != 0 && ret == 0) || endptr == buf)  {
        return 0;
    }

    return ret;
}

char * get_file_pattern(char * file_path)
{
    char * ptr;
    char * pret;

    if (file_path == NULL) {
        return NULL;
    }

    pret = malloc(MAX_PATH_LEN);
    strncpy(pret,file_path,MAX_PATH_LEN);

    ptr = strrchr(pret,'_');

    ptr++;
    if (ptr != '\0') {

        /* TODO:Is it enough that only check the first char following the last '_'? */
        if (IS_A_NUMBER(*ptr) ) {
            ptr = strrchr(pret,'_');
            *ptr = '\0';
        } else {
            ptr = strrchr(pret,'.');
            *ptr = '\0';
        }
    }

#if ENABLE_DEBUG_INFO == TRUE
    printf("get_file_pattern@pattern is %s\n",pret);
#endif

    return pret;
}

uint32 get_file_index_from_path(char * file_path)
{
    uint16 index = 0;
    uint32 ret = 0;

    char * ptr;
    char indexstr[MAX_INDEX_LEN] = {0};

    if (file_path == NULL) {
        return 0;
    }

    ptr = strrchr(file_path,'_');
    ptr++;

    while (ptr != NULL && *ptr != '\0' && *ptr != '.')  {

        if (IS_A_NUMBER(*ptr) ) {
            indexstr[index++] = *ptr++;
            ret = 1;
        } else {
            ret = 0;
            break;
        }
    }

    if (ret == 1) {
        ret = (uint32)atoi(indexstr);
    }

#if ENABLE_DEBUG_INFO == TRUE
    printf("File:%s,Number:%d\n",file_path,ret);
#endif

    return ret;
}

/* count how much lines the specified file has */
uint32 get_file_lines(char * file_path)
{
    FILE * fd;
    uint32 filenums = 0;
    char   single_file_path[MAX_PATH_LEN+1] = {0};

    if ((fd = fopen(file_path,"r")) == 0) {
        fprintf(stderr,"get_file_lines@Open %s failed\n",file_path);
        return 0;
    }

    fseek(fd, 0L, SEEK_SET);

    while (fgets(single_file_path, MAX_PATH_LEN, fd) != 0) {
        filenums++;
    }

    fclose(fd);

#if ENABLE_DEBUG_INFO == TRUE
    printf("get_file_lines@Total files:%d\n",filenums);
#endif

    return filenums;
}

/* sort linked list:  */
void slinkedlst_free(void)
{
   BLX_FILE_IN_ONE_FOLDER * bfiof_cursor = NULL;
   BLX_FILE_LIST_NODE     * bfln_cursor  = NULL;

   BLX_FILE_IN_ONE_FOLDER * bfiof_tmp = NULL;
   BLX_FILE_LIST_NODE     * bfln_tmp  = NULL;

   bfln_cursor = g_bfln_header;

   while (bfln_cursor != NULL)  {
       bfiof_cursor = bfln_cursor->first_node;

       while (bfiof_cursor != NULL)  {

           bfiof_tmp    = bfiof_cursor;
           bfiof_cursor = bfiof_cursor->next;

           free(bfiof_tmp);
       }

       bfln_tmp    = bfln_cursor;
       bfln_cursor = bfln_cursor->next_list;

       free(bfln_tmp);
   }

   return;
}

/* sort linked list:  */
void slinkedlst_dump(const void * mapped_fptr)
{
   char * fptr = (char *)mapped_fptr;

   uint32 index;

   BLX_FILE_IN_ONE_FOLDER * bfiof_cursor = NULL;
   BLX_FILE_LIST_NODE     * bfln_cursor  = NULL;

   bfln_cursor = g_bfln_header;

   while (bfln_cursor != NULL)  {
       bfiof_cursor = bfln_cursor->first_node;

       while (bfiof_cursor != NULL)  {

#if ENABLE_DEBUG_INFO == TRUE
           printf("slinkedlst_dump@listdump:%s\n", bfiof_cursor->filepath);
#endif

           index = 0;
           while ( bfiof_cursor->filepath[index] != '\0' ) {
               *fptr++ = bfiof_cursor->filepath[index++];
           }

           *fptr++ = '\n';

           bfiof_cursor = bfiof_cursor->next;
       }

       bfln_cursor = bfln_cursor->next_list;
   }

   return;
}

/* sort linked list: insert a filepath as a node into the linked list sorted */
void slinkedlst_insert(char * file_path)
{
    char * filepattern;

    BLX_FILE_IN_ONE_FOLDER * bfiof_newnode;
    BLX_FILE_LIST_NODE     * bfln_newnode;

    BLX_FILE_LIST_NODE     * bfln_cursor;
    BLX_FILE_LIST_NODE     * bfln_prv  = NULL;
    BLX_FILE_IN_ONE_FOLDER * bfiof_prv = NULL;
    BLX_FILE_IN_ONE_FOLDER * bfiof_cursor;

    bfiof_newnode = malloc(sizeof(BLX_FILE_IN_ONE_FOLDER));

    strncpy((char *)bfiof_newnode->filepath,file_path,MAX_PATH_LEN);
    bfiof_newnode->index = get_file_index_from_path(file_path);
    bfiof_newnode->next  = NULL;

    filepattern = get_file_pattern(file_path);

    if( bfiof_newnode->index == 0 ) {  /* add '_part' for the first file without index */
        sprintf(filepattern,"%s_part",filepattern);
    }

#if ENABLE_LINKED_LIST_TRACE == TRUE
    printf("insert %s\n--patter is %s\n",file_path,filepattern);
#endif

    if ( g_bfln_header == NULL )  {

        g_bfln_header = malloc(sizeof(BLX_FILE_LIST_NODE));

        strncpy((char *)g_bfln_header->pathpattern,filepattern,MAX_PATH_LEN);
        g_bfln_header->first_node = NULL;
        g_bfln_header->next_list  = NULL;
        g_bfln_header->first_node = bfiof_newnode;

#if ENABLE_LINKED_LIST_TRACE == TRUE
        printf("create header for pattern %s\n",g_bfln_header->pathpattern);
#endif
        return;
    }

    bfln_cursor  = g_bfln_header;

    /* go through Y link first */
    while (bfln_cursor != NULL)  {

        if (strcmp((char *)bfln_cursor->pathpattern,filepattern) == 0) {
            break;
        }

        bfln_prv    = bfln_cursor;
        bfln_cursor = bfln_cursor->next_list;
    }

    if (bfln_cursor == NULL) {

        bfln_newnode = malloc(sizeof(BLX_FILE_LIST_NODE));

        strncpy((char *)bfln_newnode->pathpattern,filepattern,MAX_PATH_LEN);
        bfln_newnode->first_node = bfiof_newnode;
        bfln_newnode->next_list  = NULL;

        bfln_prv->next_list  = bfln_newnode;
        free(filepattern);

#if ENABLE_LINKED_LIST_TRACE == TRUE
        printf("Add new Y node,new node is %s\n",bfiof_newnode->filepath);
#endif
        return;
    }

    bfiof_cursor = bfln_cursor->first_node;

#if ENABLE_LINKED_LIST_TRACE == TRUE
    printf("and then go through X link.\n");
#endif

    /* and then go through X link */
    while (bfiof_cursor != NULL &&
           bfiof_newnode->index > bfiof_cursor->index)
    {

        bfiof_prv      = bfiof_cursor;
        bfiof_cursor   = bfiof_cursor->next;
    }

    if (bfiof_cursor == NULL )  {

#if ENABLE_LINKED_LIST_TRACE == TRUE
         printf("...insert at the end of X link\n");
#endif
         bfiof_prv->next = bfiof_newnode;
    } else {

#if ENABLE_LINKED_LIST_TRACE == TRUE
         printf("...insert at the middle of X link\n");
#endif
         bfiof_prv->next     = bfiof_newnode;
         bfiof_newnode->next = bfiof_cursor;
    }

    free(filepattern);
}

void sort_filelist(char * path)
{
    int    fd;
    uint32 len = 0;
    uint32 map_file_size;
    void * mapped_fptr;
    char * fptr;
    char   single_file_path[MAX_PATH_LEN+1] = {0};

    fd  = open(path,O_RDWR);
    map_file_size = lseek(fd, 1L, SEEK_END) + MAX_PATH_LEN;

    mapped_fptr = (void *)mmap(NULL,map_file_size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);

#if ENABLE_DEBUG_INFO == TRUE
    printf("sort_filelist@create map file done:\n%s\nsize:%d",(char *)mapped_fptr,map_file_size);
#endif

    fptr = (char *)mapped_fptr;

    /* go through the list file that contains all blx files' full path line by line */
    while ( fptr != NULL && *fptr != '\0' ) {

        if ( *fptr == 0x0A ) {
            len = 0;

#if ENABLE_DEBUG_INFO == TRUE
            printf("sort_filelist@get one file path from mapped file:%s\n",single_file_path);
#endif
            slinkedlst_insert(single_file_path);

            memset(single_file_path,0x0,MAX_PATH_LEN);
            fptr++;
            continue;

        } else {
            single_file_path[len] = *fptr;
        }

        fptr++;
        len++;
    }

    /* write sorted list back to the file */
    slinkedlst_dump(mapped_fptr);

    msync(mapped_fptr, map_file_size, MS_ASYNC);
    munmap(mapped_fptr, map_file_size);

    slinkedlst_free();
    close(fd);

    return;
}

uint16 decode_timestamp(uint8 * bytestream,  /* input,a 8 bytes stream */
                       char * timestring)    /* output */
{
    uint64 value = 0;
    uint64 totalSeconds;

    uint32 seconds = 0;
    uint32 minutes = 0;
    uint32 hours   = 0;

    uint16 timestring_len = 0;
 
    const uint32 digits_after_point = 9; /* e.g. when the number is 3, time format like 12:49:29.537
                                                 when the number is 6, time format like 12:49:29.537030 */

    double fraction;
    uint32 digits;

    if (bytestream == NULL || timestring == NULL) {
        return 0;
    }

    value  = (uint64)bytestream[0] << 56        |
             (uint64)bytestream[1] << 56 >>8    |
             (uint64)bytestream[2] << 56 >>16   |
             (uint64)bytestream[3] << 56 >> 24  |
             (uint64)bytestream[4] << 56 >> 32  |
             (uint64)bytestream[5] << 56 >> 40  |
             (uint64)bytestream[6] << 56 >> 48  |
             (uint64)bytestream[7];

    if ((value & 0xF000000000000000LL) != 0)  {
        value = value & 0x0FFFFFFFFFFFFFFFLL;

        /* if 60th bit is not set, the timestamp is either before 1989, or after mid 2006 (in which case there was an overrun)
           since we don't have timestamps before 1989, we always set the 61th bit to one to account for the overrun
         */
        if ((value & 0x0800000000000000LL) == 0)  {
            value = value | 0x1000000000000000LL;
        }
    }

    totalSeconds = value / TIME_UNIT;

    fraction = (value % TIME_UNIT) / (double)TIME_UNIT;
    digits   = fraction * pow(10.0,(int)digits_after_point);

    seconds = totalSeconds % SECONDS_FOR_ONE_DAY;
    minutes = seconds / 60;
    hours   = minutes / 60;

    sprintf(timestring, "%02d:%02d:%02d.%0*d", hours, minutes % 60, seconds % 60, (int)digits_after_point, digits);
    timestring_len = strlen(timestring);

    return timestring_len;
}

void halloc_info_linkedlst_free(void)
{
    HEAP_LINK_NODE * hln = g_heap_link;

    while(hln != NULL) {
        hln = hln->next;
        free(g_heap_link);
        g_heap_link = hln;    
    }
 
    return;
}

/* TODO: performance bottleneck!!!
   heap allocation infor linked list: */
uint32 halloc_info_linkedlst_get_size(uint32 addr)
{
    HEAP_LINK_NODE * hln_cursor = g_heap_link;
    HEAP_LINK_NODE * hln_prv    = g_heap_link;
    uint32 size = 0;

    while (hln_cursor != NULL) {

        if (hln_cursor->addr == addr) {
            
            /* find it! remove the node from link and return size */
            size = hln_cursor->size;
 
            if (hln_cursor == g_heap_link) {

                /* remove the first node... */
                g_heap_link  = g_heap_link->next;
           
                free(hln_prv);

                hln_cursor = g_heap_link;             
                hln_prv    = g_heap_link;

            } else {

                hln_prv->next = hln_cursor->next;
                free(hln_cursor);
            }            

            break;    
        } else {

            hln_prv    = hln_cursor;
            hln_cursor = hln_cursor->next;
        }
    }

#if ENABLE_TRACE_INFO == TRUE
    if (size == 0) {
        printf("Warning:No allocation found for 0x%X\n",addr);
    }
#endif

    return size;   
}

/* heap allocation infor linked list
   when alloca heap, add address and size into the linked list. when dealloc, caller needs to know
   the heap size of the deallocation request
   
   see HEAP_LINK_NODE
 */
void halloc_info_linkedlst_add(uint32 addr,uint32 size)
{
    HEAP_LINK_NODE * hln_newnode;

    hln_newnode       = malloc(sizeof(HEAP_LINK_NODE));
    hln_newnode->addr = addr;
    hln_newnode->size = size;
    hln_newnode->next = NULL;

    if (g_heap_link == NULL) {
       g_heap_link = hln_newnode;
    } else {

       /* just add at the beginnin of the linked list */
       hln_newnode->next = g_heap_link;
       g_heap_link       = hln_newnode;    
    }

    return;
}

uint8 scan_single_meta_file(FILE * fd_rd,FILE * fd_wr)
{
    uint8 bret = FALSE;

    META_FORMAT_UNIT * meta_unit;
    uint32 hour,minute,second;
    uint32 size,allocation_type;

    char line_rd[MAX_SINGLE_METADATA_LEN] = {0};
    char line_wr[MAX_SINGLE_METADATA_LEN] = {0};
    char temp[3]; 
    static uint32 last_hours = INVALID_HOUR;

    uint32 addr;

    while (fgets(line_rd,MAX_SINGLE_METADATA_LEN, fd_rd) != 0) {

        meta_unit = (META_FORMAT_UNIT *)line_rd;
        //meta_unit->skip6     = '\0';
        //meta_unit->mft.skip4 = '\0';
        temp[0] = meta_unit->mft.hour[0];temp[1] = meta_unit->mft.hour[1];temp[2]='\0';
        hour   = strtouint32(temp);
		
		temp[0] = meta_unit->mft.minute[0];temp[1] = meta_unit->mft.minute[1];
        minute = strtouint32(temp);
		
		temp[0] = meta_unit->mft.second[0];temp[1] = meta_unit->mft.second[1];
        second = strtouint32(temp);

        if (last_hours == INVALID_HOUR) {
            last_hours = hour;
        } else if (hour != last_hours)  {
            last_hours++;
        }

        // TODO:leap year
        if (last_hours > 24) {
            g_trace_date.day++;
            last_hours = hour;
        }

        size   = strtouint32(meta_unit->size);
        allocation_type = meta_unit->allocation_type;

        addr  = (uint32)char_2_hex(meta_unit->address[0]) << 28   |
                (uint32)char_2_hex(meta_unit->address[1]) << 24   |
                (uint32)char_2_hex(meta_unit->address[2]) << 20   |
                (uint32)char_2_hex(meta_unit->address[3]) << 16   |
                (uint32)char_2_hex(meta_unit->address[4]) << 12   |
                (uint32)char_2_hex(meta_unit->address[5]) << 8    |
                (uint32)char_2_hex(meta_unit->address[6]) << 4    |
                (uint32)char_2_hex(meta_unit->address[7]);

        switch (meta_unit->type)
        {
            case TYPE_ALLOCATE:
                  halloc_info_linkedlst_add(addr,size);
                  g_free_heap -= size;
                  sprintf(line_wr,"%02d/%02d/%04d %02d:%02d:%02d.%s, %08d\n",
                          g_trace_date.day,g_trace_date.month,g_trace_date.year,
                          hour,minute,second,meta_unit->mft.ms,g_free_heap);
                  break;

            case TYPE_DEALLOCATE:
                  g_free_heap += halloc_info_linkedlst_get_size(addr);
                  sprintf(line_wr,
                          "%02d/%02d/%04d %02d:%02d:%02d.%s, %08d\n",
                          g_trace_date.day,g_trace_date.month,g_trace_date.year,
                          hour,minute,second,meta_unit->mft.ms,g_free_heap);
                  break;

            default:
                  assert(1);
        }        

        fwrite(line_wr,strlen(line_wr),1,fd_wr);
    }

    bret = TRUE;
    return bret;
}

uint8 build_csv()
{
    uint8 bret = FALSE;
 
    FILE * fd_meta_list;
    FILE * fd_csv;
    FILE * fd_meta;
    FILE * fd_date;
 
    uint16 len;

    struct timeval startTime;
    struct timeval endTime;

    double wall_clock_counter = 0;
    
    char single_file_path[MAX_PATH_LEN] = {0};

    if ((fd_meta_list = fopen(META_FILE_LIST,"r")) == 0) {
        fprintf(stderr,"build_csv@1@Read %s failed\n",META_FILE_LIST);
        return bret;
    }

    system(REMOVE_DEFAULT_META_FILE);
    if ((fd_csv = fopen(DEFAULT_META_FILE,"a")) == 0) {
        fclose(fd_meta_list); 
        fprintf(stderr,"Create %s failed\n",DEFAULT_META_FILE);
        return bret;
    }

    if ((fd_date = fopen(TRACE_START_DATE,"rb")) == 0) {
        SET_DEFAULT_START_DATE(g_trace_date);
    } else {

        fseek(fd_date, 0L, SEEK_SET);
        fread((void *)&g_trace_date, sizeof(TRACE_DATE), 1,fd_date);
        fclose(fd_date);
    }   

    /* get the current time(wall-clock time)
       - NULL because we don't care about time zone
     */
    gettimeofday(&startTime, NULL);
    fprintf(stdout,"Generating....\n");

    bret = TRUE;

    while (fgets(single_file_path, MAX_PATH_LEN, fd_meta_list) != 0) {

        /* remove 0x0D and 0x0A from the new line,otherwise ifstream can not work then...*/
        len = strlen(single_file_path);
        if (single_file_path[len-1] == 0x0A)  {
           single_file_path[len-1] = 0x0;
        }

        if ((fd_meta = fopen(single_file_path,"r")) == 0)  {
           fprintf(stderr,"build_csv@2@Read %s failed\n",single_file_path);
           bret = FALSE;
           break;
        }

        if (scan_single_meta_file(fd_meta,fd_csv) == FALSE)  {
           fprintf(stderr,"Scan %s failed\n",single_file_path);
           fclose(fd_meta);
           bret = FALSE;
           break;
        }

        fclose(fd_meta);

    } /* end while */

    fclose(fd_meta_list);
    fclose(fd_csv);

    /* get the end time */
    gettimeofday(&endTime, NULL);

    /* calculate time in microseconds */
    wall_clock_counter = (endTime.tv_sec*1000000  + (endTime.tv_usec)) - (startTime.tv_sec*1000000 + (startTime.tv_usec));
    fprintf(stdout,"------------------------------------------------------\n");
    fprintf(stdout,"Time cost:%f minutes\n",wall_clock_counter/(1000000*60));

    return bret;
}

uint8 metadata_single_blx_file(uint32 fileindex,char * filepath)
{
    uint8 bret = FALSE;

    FILE * blx_file;
    FILE * fd_meta;

    uint8 cursor = 0;
    HEAP_TRACE_INFO hti;
    
    char time_stamp[32] = {0};

    char metadata[MAX_SINGLE_METADATA_LEN] = {0};
    char meta_file[MAX_PATH_LEN]; 

    memset(&hti,0x0,sizeof(HEAP_TRACE_INFO));

    blx_file = fopen(filepath, "rb");
    if (blx_file == NULL)  {
       fprintf(stderr,"Could not open %s\n",filepath);
       return bret;
    }

    sprintf(meta_file,"%s%s.%d%s",DEFAULT_META_FOLDER_PREFIX,basename(filepath),fileindex,DEFAULT_META_FILE_SUFFRIX);
    fd_meta = fopen(meta_file, "a");
    if (fd_meta == NULL)  {
       fclose(blx_file);
       fprintf(stderr,"Could not create %s\n",meta_file);
       return bret;
    }

    fseek(blx_file, 0L, SEEK_SET);

    /*
       Go through the file to find Message id(0x94) first. If find it then check if the next byte is Master(0x01). If yes, the 
       read HEAP_TRACE_INFO for further check
       TODO: how to seek to real trace content directly?
     */
    while (!feof(blx_file))   {

        if (fread(&cursor, sizeof(uint8), 1, blx_file) != 1) {
            break;
        }

        /* is it a Message id(0x94)? */
        if (SIGNATURE_MESSAGE_ID != cursor ) {
            continue;
        }

        if (fread(&cursor, sizeof(uint8), 1, blx_file) != 1) {
            break;
        }

        /* is it a Master(0x01)? */
        if (SIGNATURE_MASTER != cursor ) {
            fseek(blx_file, -1L, SEEK_CUR); /* if not then back 1 byte in case the byte is Message id... */
            continue;
        }

        /* now we get '0x94,0x01' in blx,which means it may a heap message */
        if (fread(&hti, sizeof(hti), 1, blx_file) != 1) {
            break;
        }

        if (hti.type != SIGNATURE_HEAP_TYPE ) {
            fseek(blx_file, -sizeof(hti)-1L, SEEK_CUR); /* if not then back sizeof(hti) bytes... */
            continue;
        }

        switch (hti.id)
        {
            case SIGNATURE_HEAP_DEALLOC:
            case SIGNATURE_HEAP_ALLOC:
            case SIGNATURE_HEAP_ALLOC_NO_WAIT:
            case SIGNATURE_HEAP_COND_ALLOC:
            case SIGNATURE_ALIGNED_ALLOC_NO_WAIT:
            case SIGNATURE_ALIGNED_ALLOC:
            case SIGNATURE_HEAP_ALLOC_NO_WAIT_FROM:
                decode_timestamp(&hti.time[0],time_stamp);
                break;

            default:
                fseek(blx_file, -sizeof(hti)-1L, SEEK_CUR); /* if not then back sizeof(hti) bytes... */
                continue;
        }
        
        switch (hti.id)
        {
            case SIGNATURE_HEAP_DEALLOC:            
                // printf("%s HOOK_HEAP_ALLOC_DEALLOC,ptr=0x%X\n",trace_time,GET_PTR(hti.ptr));
                // fprintf(stdout,"%s,HEAP_DEALLOC,free size=%d(-%d)\n",trace_time,g_free_heap,freed_size);
                sprintf(metadata,META_DATA_FORMAT,time_stamp,TYPE_DEALLOCATE,GET_PTR(hti.ptr),0,0,
                        GET_PTR(hti.hdt.caller1),GET_PTR(hti.hdt.caller2));
                fwrite(metadata,strlen(metadata),1,fd_meta);
                break;

            case SIGNATURE_HEAP_ALLOC:
                sprintf(metadata,META_DATA_FORMAT,time_stamp,TYPE_ALLOCATE,GET_PTR(hti.ptr),GET_SIZE(hti.hat.size),AT_HEAP_ALLOC,
                        GET_PTR(hti.hat.caller1), GET_PTR(hti.hat.caller2));
                fwrite(metadata,strlen(metadata),1,fd_meta);
                break; 
            case SIGNATURE_HEAP_ALLOC_NO_WAIT:
                sprintf(metadata,META_DATA_FORMAT,time_stamp,TYPE_ALLOCATE,GET_PTR(hti.ptr),GET_SIZE(hti.hat.size),AT_HEAP_ALLOC_NO_WAIT,
                        GET_PTR(hti.hat.caller1), GET_PTR(hti.hat.caller2));
                fwrite(metadata,strlen(metadata),1,fd_meta);
                break; 
            case SIGNATURE_HEAP_COND_ALLOC:
                sprintf(metadata,META_DATA_FORMAT,time_stamp,TYPE_ALLOCATE,GET_PTR(hti.ptr),GET_SIZE(hti.hat.size),AT_HEAP_COND_ALLOC,
                        GET_PTR(hti.hcat.caller1), GET_PTR(hti.hcat.caller2));
                fwrite(metadata,strlen(metadata),1,fd_meta);
                break; 
            case SIGNATURE_ALIGNED_ALLOC_NO_WAIT:
                sprintf(metadata,META_DATA_FORMAT,time_stamp,TYPE_ALLOCATE,GET_PTR(hti.ptr),GET_SIZE(hti.hat.size),AT_ALIGNED_ALLOC_NO_WAIT,
                        GET_PTR(hti.haat.caller1), GET_PTR(hti.haat.caller2));
                fwrite(metadata,strlen(metadata),1,fd_meta);
                break; 
            case SIGNATURE_ALIGNED_ALLOC:
                sprintf(metadata,META_DATA_FORMAT,time_stamp,TYPE_ALLOCATE,GET_PTR(hti.ptr),GET_SIZE(hti.hat.size),AT_ALIGNED_ALLOC,
                        GET_PTR(hti.haat.caller1), GET_PTR(hti.haat.caller2));
                fwrite(metadata,strlen(metadata),1,fd_meta);
                break; 
            case SIGNATURE_HEAP_ALLOC_NO_WAIT_FROM:
                sprintf(metadata,META_DATA_FORMAT,time_stamp,TYPE_ALLOCATE,GET_PTR(hti.ptr),GET_SIZE(hti.hat.size),AT_ALLOC_NO_WAIT_FROM,
                        GET_PTR(hti.hanwft.caller1), GET_PTR(hti.hanwft.caller2));
                fwrite(metadata,strlen(metadata),1,fd_meta);
                break; 
            default:
                fseek(blx_file, -sizeof(hti)-1L, SEEK_CUR); /* if not then back sizeof(hti) bytes... */
                continue;
        }

    } /* end while */

    fclose(blx_file);
    fclose(fd_meta);

    bret = TRUE;
    return bret;
}

void * working_thread(void * arg)
{
    THREAD_PARAMETER * tp = malloc(sizeof(THREAD_PARAMETER));
 
    tp->fileindex = ((THREAD_PARAMETER *)arg)->fileindex;
    tp->threadsid = ((THREAD_PARAMETER *)arg)->threadsid;
    strncpy(tp->filepath,((THREAD_PARAMETER *)arg)->filepath,MAX_PATH_LEN);

    MUTEX_LOCK(g_thread_flag_mutex);
    g_thread_pool[tp->threadsid].busy = TRUE;
    MUTEX_UNLOCK(g_thread_flag_mutex);

    if (!metadata_single_blx_file(tp->fileindex,tp->filepath))  {
        fprintf(stderr, "Thread%d returns failed\n",tp->threadsid);
    }

    MUTEX_LOCK(g_thread_flag_mutex);
    g_thread_pool[tp->threadsid].busy = FALSE;
    MUTEX_UNLOCK(g_thread_flag_mutex);

    fprintf(stdout,"Thread%d done\n",tp->threadsid);
    free(tp);

    pthread_exit(NULL);
}

uint8 build_metadata(void)
{
    uint8 bret = FALSE;

    FILE * fd_blx_list_file;
    FILE * fd_meta_list_file;
    FILE * fd_date_file;

    char   single_file_path[MAX_PATH_LEN+1] = {0};
    char   meta_file_path[MAX_PATH_LEN+1] = {0};
    char   commandstr[128];

    uint32 fileindex;
    uint32 len,filenums = 0;
    uint16 lots_of_threads;

    struct timeval startTime;
    struct timeval endTime;

    double wall_clock_counter = 0;

    static uint8 has_start_date = FALSE; /* we need an initialization date to cover 120 hours timeline */
    struct stat stbuf;
    struct tm * tm_date;

    THREAD_PARAMETER tp;  /* each thread will have a copy of input parameter! */

    system(REMOVE_DEFAULT_META_FOLDER);
    system(CREATE_DEFAULT_META_FOLDER);

    sprintf(commandstr,"ls -ogh -lrtd $(find $PWD -type f -name '*.blx') | awk '{print $NF}' > %s",TEMPLATE_FILE_LIST);    
    if (system(commandstr) != 0) {
        fprintf(stderr, "Can not get source file list, please check your shell or path\n"); 
        return bret;
    }

    filenums = get_file_lines(TEMPLATE_FILE_LIST);
    switch ( filenums ) 
    { 
        case 0:
            return bret;
        case 1:
            break;
        default:
            /* check if files are sorted in tmp_blx_file_list based on modification time
               if not, then correct it
             */
            sort_filelist(TEMPLATE_FILE_LIST);
    }

    if ((fd_blx_list_file = fopen(TEMPLATE_FILE_LIST,"r")) == 0) {
        fprintf(stderr,"build_metadata@1@Read %s failed\n",TEMPLATE_FILE_LIST);
        return bret;
    }

    fseek(fd_blx_list_file, 0L, SEEK_SET);

    if ((fd_meta_list_file = fopen(META_FILE_LIST,"a")) == 0) {
        fclose(fd_blx_list_file);
        fprintf(stderr,"build_metadata@2@Read %s failed\n",META_FILE_LIST);
        return bret;
    }

    /* init thread mutex and pool */
    pthread_mutex_init(&g_thread_flag_mutex, NULL);
    for (lots_of_threads = 0; lots_of_threads < MAX_NUM_THREADS; lots_of_threads++)   {
        MUTEX_LOCK(g_thread_flag_mutex);
        g_thread_pool[lots_of_threads].pt   = 0;
        g_thread_pool[lots_of_threads].busy = FALSE;
        MUTEX_UNLOCK(g_thread_flag_mutex);
    }

    /* get the current time(wall-clock time)
       - NULL because we don't care about time zone
     */
    gettimeofday(&startTime, NULL);
    fileindex = 0;

    while (fgets(single_file_path, MAX_PATH_LEN, fd_blx_list_file) != 0) {

        /* remove 0x0D and 0x0A from the new line,otherwise ifstream can not work then...*/
        len = strlen(single_file_path);
        if (single_file_path[len-1] == 0x0A)  {
           single_file_path[len-1] = 0x0;
        }

        /* get first file's date as base date */ 
        if (has_start_date == FALSE || g_trace_date.day == 0)  {
            if (stat(single_file_path, &stbuf) == -1) {
                SET_DEFAULT_START_DATE(g_trace_date);
                fprintf(stderr,"fstat failed, What's wrong with the file!?\n");
            }

            if (S_ISREG(stbuf.st_mode)) {
                tm_date = localtime(&stbuf.st_mtime);

                g_trace_date.day   = tm_date->tm_mday;
                g_trace_date.month = tm_date->tm_mon+1;
                g_trace_date.year  = tm_date->tm_year+1900;

                has_start_date = TRUE;
               
            } else {
                //TODO: What can I do without modification time?
                SET_DEFAULT_START_DATE(g_trace_date);
                fprintf(stderr,"It is NOT a regual file!?\n");
            }

            if ((fd_date_file = fopen(TRACE_START_DATE,"wb")) == 0) {
                SET_DEFAULT_START_DATE(g_trace_date);                
            } else {
                fwrite((const void *)&g_trace_date,sizeof(g_trace_date),1,fd_date_file);
            }
        }        

FIND_FREE_THREAD:
        lots_of_threads = 0;

        MUTEX_LOCK(g_thread_flag_mutex);
        
        /* try to find a free thread from pool to decode the file */        
        for (lots_of_threads = 0; lots_of_threads < MAX_NUM_THREADS; lots_of_threads++)  {

            if (g_thread_pool[lots_of_threads].busy == FALSE)  { 

                g_thread_pool[lots_of_threads].busy = TRUE;
                tp.threadsid = lots_of_threads;
                tp.fileindex = fileindex++;
                strncpy(tp.filepath,single_file_path,MAX_PATH_LEN);

                sprintf(meta_file_path,"%s%s.%d%s\n",DEFAULT_META_FOLDER_PREFIX,basename(single_file_path),tp.fileindex,DEFAULT_META_FILE_SUFFRIX);
                fwrite(meta_file_path,strlen(meta_file_path),1,fd_meta_list_file);
                   
                fprintf(stdout, "Processing %s by thread%d...\n",tp.filepath,tp.threadsid);
                if (pthread_create(&(g_thread_pool[lots_of_threads].pt), NULL,working_thread, (void *)&tp) != 0)  {
                   fprintf(stderr,"Working thread creation failed when processing %s\n",single_file_path);
                   exit(EXIT_FAILURE);
                }

                sleep(1);
                break;
            } 
        }

        MUTEX_UNLOCK(g_thread_flag_mutex);
        
        if (lots_of_threads >= MAX_NUM_THREADS) {

            /* wait for a free thread is all threads are busying...             
             */ 
            fprintf(stdout,"Waiting for free thread...\n");
            sleep(1);
            goto FIND_FREE_THREAD;
        }

        /* Is it necessary here? */
        memset(single_file_path,0x0,MAX_PATH_LEN);

    } /* end-while */

    for (lots_of_threads = 0; lots_of_threads < MAX_NUM_THREADS; lots_of_threads++)  {

        if (g_thread_pool[lots_of_threads].pt) {
            pthread_join(g_thread_pool[lots_of_threads].pt,NULL);
        }
    }

    pthread_mutex_destroy(&g_thread_flag_mutex);

    fclose(fd_blx_list_file);
    fclose(fd_meta_list_file);

    /* get the end time */
    gettimeofday(&endTime, NULL);

    /* calculate time in microseconds */
    wall_clock_counter = (endTime.tv_sec*1000000  + (endTime.tv_usec)) - (startTime.tv_sec*1000000 + (startTime.tv_usec));

    fprintf(stdout,"---------------------------------------------------\n");
    fprintf(stdout,"Time cost:%f minutes\n",wall_clock_counter/(1000000*60));

    bret = TRUE;

    return bret;
}

uint8 sampling_csv_from_meta(uint64 sample_rate)
{
    uint8 bret = FALSE;

    FILE * fd_metadata;
    FILE * fd_csv;

    uint64 counter = 0; 

    char single_file_path[MAX_PATH_LEN+1] = {0};
    char csv_file[MAX_PATH_LEN] = {0};

    if (sample_rate <= 1) {
        return bret; /* just same as meta data */ 
    }

    if ((fd_metadata = fopen(DEFAULT_META_FILE,"r")) == 0) {
        printf("Read metadata file failed\n");
        return bret;
    }

    sprintf(csv_file,"rm -f ./%s%llu.csv",DEFAULT_CSV_FILE_PREFIX,sample_rate);
    system(csv_file);

    sprintf(csv_file,"./%slines_%llu.csv",DEFAULT_CSV_FILE_PREFIX,sample_rate);
    if ((fd_csv = fopen(csv_file,"a")) == 0) {
        fprintf(stderr,"Create csv file failed\n");
        return bret;
    }

    while (fgets(single_file_path, MAX_PATH_LEN, fd_metadata) != 0)  {

        if (counter == 0) {
            /* always need the first line of meta data because it is the first point of Y axis*/ 
            fwrite(single_file_path,strlen(single_file_path),1,fd_csv);
            counter++;  
            continue;
        }

        if (counter++ < sample_rate) {
            continue;    
        }

        fwrite(single_file_path,strlen(single_file_path),1,fd_csv);
        counter = 1;
    }

    fclose(fd_metadata);
    fclose(fd_csv);

    bret = TRUE;
    return bret;
}

void show_usage(void)
{
    fprintf(stdout,"Usage: ma <option>\r\n");
    fprintf(stdout,"Options:\r\n");
    fprintf(stdout,"  Currently, the following options are supported...\r\n");
    fprintf(stdout,"   -b                    build meta data by scanning all blx files recursively and generate metadata at %s\r\n",DEFAULT_META_FOLDER_PREFIX);
    fprintf(stdout,"   -r                    output general heap usage\r\n");
    fprintf(stdout,"   -s <sampling rate>    generat a new csv with specified sampling rate\r\n");
    fprintf(stdout,"   -g <minutes>          generat a new csv by sampling every <minutes>\r\n");
    fprintf(stdout,"\r\n");
}

/* -t <minutes> option
      sampling for every <minutes>
 */
uint8 opt_handler_t(char * time)
{
    uint8 bret = FALSE;

    uint32 time_step = strtouint32(time);
    TRACE_TIME time_next;
    TRACE_TIME time_current;

    uint8 first_line = TRUE;
    char temp[3];

    FILE * fd_meta_csv;
    FILE * fd_new_csv;
    
    uint16 d,h,m;

    META_DATE * tmd;

    char new_file[MAX_PATH_LEN];
    char commandstr[MAX_PATH_LEN];
    char line_rw[MAX_SINGLE_METADATA_LEN] = {0};

    if (time_step == 0) {
        return bret;
    }

    sprintf(new_file,"./%s%dminutes.csv",DEFAULT_CSV_FILE_PREFIX,time_step);
    sprintf(commandstr,"rm -f %s",new_file);
    system(commandstr);

    if( (fd_meta_csv = fopen(DEFAULT_META_FILE,"r")) == 0) {
        fprintf(stderr,"Can not open :%s\n",DEFAULT_META_FILE);
    }

    if( (fd_new_csv = fopen(new_file,"a")) == 0) {
        fprintf(stderr,"Can not open :%s\n",new_file);
    }

    while (fgets(line_rw, MAX_SINGLE_METADATA_LEN, fd_meta_csv) != 0)  {

        tmd = (META_DATE *)line_rw;

        temp[0] = tmd->minute[0];temp[1] = tmd->minute[1];temp[2] = '\0';
        time_current.minute = strtouint32(temp);

        temp[0] = tmd->hour[0];temp[1] = tmd->hour[1];
        time_current.hour = strtouint32(temp);

        temp[0] = tmd->day[0];temp[1] = tmd->day[1];
        time_current.day = strtouint32(temp);

        if (first_line == TRUE) {
            /* always need the first line of meta data because it is the first point of Y axis*/ 
            fwrite(line_rw,strlen(line_rw),1,fd_new_csv);

            /* update time if necessary... */
            time_next.minute = (time_current.minute+time_step)%60;
            time_next.hour   = (time_current.hour + (time_current.minute+time_step)/60)%24;
            time_next.day    = time_current.day + time_current.hour/24;

            first_line = FALSE;
            continue;

        } 

        if (time_current.day < time_next.day)  {
            continue;
        } 

        if (time_current.hour < time_next.hour) {
            continue;
        }

        if (time_current.minute < time_next.minute)  {
            continue;
        }

        /* update time... */
        m = (time_current.minute+time_step)%60;
        h = (time_current.hour + (time_current.minute+time_step)/60)%24;
        d = time_current.day + time_current.hour/24;

        if (time_next.minute == m && time_next.hour == h && time_next.day == d ) {
            continue;
        }

        time_next.minute = m;
        time_next.hour   = h;
        time_next.day    = d;

        fwrite(line_rw,strlen(line_rw),1,fd_new_csv);
    }

    fclose(fd_meta_csv);
    fclose(fd_new_csv);

    bret = TRUE;
    return bret;
}

/* -s <lines> option
      set the sampling rate by specifying the lines of the increment on the X-axis between lines in the full csv file
      generated by scan_all function. 

      For example, the original full csv file has 90000 lines. You can reduce the sampling rate by getting a sample point for every 1000 line
        $ma -s 10000
 */
uint8 opt_handler_s(char * in)
{
    uint8 bret = FALSE;
    char * sampling_rate_lines = 0;
    char * endptr = NULL; 
    uint64 sample_rate;

    sampling_rate_lines = in;
         
    /* To distinguish success/failure after call */
    errno = 0;
    sample_rate = strtoll(sampling_rate_lines, &endptr, 10);

    /* Check for various possible errors */
    if ( errno == ERANGE || (errno != 0 && sample_rate == 0) || endptr == sampling_rate_lines)  {
        return bret;
    }

    sampling_csv_from_meta(sample_rate);

    bret = TRUE;
    return bret;
}

/* -r option
      output a brief report(TODO: only for heap right now) 
 */
uint8 opt_handler_r(char * in)
{
    uint8 bret = FALSE;

	FILE * fd_meta_csv; 
	
    CSV_FORMAT_UNIT * csv_unit;
    static uint32 bottom_heap_size = MAX_THEORY_HEAP_SIZE;

    char line_rd[MAX_SINGLE_METADATA_LEN] = {0};
	char line_tm[MAX_SINGLE_METADATA_LEN] = {0};

    if( (fd_meta_csv = fopen(DEFAULT_META_FILE,"r")) == 0) {
        fprintf(stderr,"Can not open :%s\n",DEFAULT_META_FILE);
		return bret; 
    }
	
    while (fgets(line_rd, MAX_SINGLE_METADATA_LEN, fd_meta_csv) != 0)  {
        csv_unit   = (CSV_FORMAT_UNIT *)line_rd;
		
        if (strtouint32(csv_unit->freesize) < bottom_heap_size) {
		    bottom_heap_size = strtouint32(csv_unit->freesize);
			strcpy(line_tm,line_rd);
			continue;
		} 
    }

    fclose(fd_meta_csv);
	
	fprintf(stdout,"Bottom heap size is %d.\n%s\n",bottom_heap_size,line_tm);

    bret = TRUE;
    return bret;
}

uint8 opt_handler_z(uint32 starttime,uint32 endtime)
{
    uint8 bret = FALSE;
    //TODO:
    return bret;
}

sint32 get_expression_result(char * argv)
{
    sint32 ret = 0;
    char * time_value = 0; 
    char * endptr     = NULL; 

    time_value = argv;
         
    /* To distinguish success/failure after call */
    errno = 0;
    ret = strtol(time_value, &endptr, 10);

    /* Check for various possible errors */
    if ( errno == ERANGE || (errno != 0 && ret == 0) || endptr == time_value)  {
        ret = -1;
    }

    printf("ret=%d\n",ret);
    return ret;
}

int main(int argc, char * argv[])
{
    sint32 start_time;
    sint32 end_time;
    uint8 bret = FALSE;

////////////////////////////////////////////////////////////////////////////////////
    switch (argc)
    {
       case 2:  /* --help, -r, -b */
           if (argv[1][0] != '-' ) {
                goto MISSING_OR_WRONG_OPTIONS;
           } 
              
           switch (argv[1][1])
           {
               case '-':
                   if (argv[1][2] == 'h' && argv[1][3] == 'e' &&  argv[1][4] == 'l' && argv[1][5] == 'p')   
                   {
                       show_usage();
                       return 1;
                   }
                   break;

               case 'b':                      /* -b, build meta data by scanning all blx files recursively */
                   bret = build_metadata();
                   break;

               case 'g':                      /* -g, build big csv based on meta files                     */
                   bret = build_csv();
                   break;           

               case 'r':                      /* -r, generate general heap report  */
                   //TODO
                   bret = opt_handler_r(argv[2]);   
                   break;


               default: 
                    break;
           }
           break;

       case 3:  /* -s, -g */
           if (argv[1][0] != '-' ) {
                goto MISSING_OR_WRONG_OPTIONS;
           } 
           
           switch (argv[1][1])
           {
               case 's':               /* -s <sample rate>, generate csv by specified sampling rate */
                   bret = opt_handler_s(argv[2]);
                   break;

               case 't':               /* -t <minutes>, generate csv by every minutes */
                   bret = opt_handler_t(argv[2]);
                   break;
    
               default:
                   break;
           }           
           break;

       case 4: /* -z*/                 
           if (argv[1][0] != '-' ) {
                goto MISSING_OR_WRONG_OPTIONS;
           }

           switch (argv[1][1])
           {
               case 'z':              /* -z <beginning_time> <end_time>, generate csv between start and end time  */
                   start_time = get_expression_result(argv[2]);
                   end_time   = get_expression_result(argv[3]);
                   if (start_time > 0 && end_time > 0 && start_time < end_time) 
                   {
                       //TODO
                       bret = opt_handler_z((uint32)start_time, (uint32)end_time);
                   }
               default:
                   break;
           }
           break;

       default:
MISSING_OR_WRONG_OPTIONS:
           fprintf(stdout, "see: missing usage\n");
           fprintf(stdout, "Try `see --help' for more information.\n");
           break;
    }

    return 1;
}


