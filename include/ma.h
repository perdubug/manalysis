#ifndef MA_H
#define MA_H

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

#include "types.h"

/************************************************************************** 
   for decode blx...use to identify MTBF blx format
 **************************************************************************/
/* media type */
#define MEDIA_TYPE_TCPIP 0x1D
#define MEDIA_TYPE_USB   0x1B

/* receiver device */
#define RECEIVER_DEVICE_PC 0x10

/* send device */
#define SEND_DEVICE_TRACEBOX 0x4C

/* resource */
#define RESOURCE_TRACEBOX 0x7C

/************************************************************************** 
   Macros...
 **************************************************************************/
/* TODO:use the real one */
#define DEFAULT_TOTAL_FREE_HEAP (6.9*1024*1024)   /* default is 6M free heap */
#define BLX_STARTING_POINT 0xbb4                  /* this offset is based on my observation */

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
    Operation Type:   + means deallocation, - means allocation, $ - means start(heap_init)
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

#define TYPE_INIT       '$'
#define TYPE_ALLOCATE   '-'
#define TYPE_DEALLOCATE '+'

#define MAX_PATH_LEN             512
#define MAX_INDEX_LEN            5
#define MAX_SINGLE_METADATA_LEN  128
#define MAX_THEORY_HEAP_SIZE     0xFFFFFFFF

#define MAX_NUM_THREADS          7  /* must lower than MAXT_IN_POOL */

#define ENABLE_TRACE_GENERAL     FALSE
#define ENABLE_TRACE_INFO        FALSE
#define ENABLE_DEBUG_INFO        FALSE
#define ENABLE_LINKED_LIST_TRACE FALSE
#define ENABLE_HEAP_TRACE        FALSE

#define TRACE_TYPE_DEFAULT 0
#define TRACE_TYPE_NOS     1

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

#define SIGNATURE_HEAP_INIT               0x79  /* HOOK_HEAP__INIT                  */

#define red   "\033[0;31m"        /* 0 -> normal ;  31 -> red */
#define cyan  "\033[1;36m"        /* 1 -> bold ;  36 -> cyan */
#define green "\033[4;32m"        /* 4 -> underline ;  32 -> green */
#define blue  "\033[9;34m"        /* 9 -> strike ;  34 -> blue */
#define black  "\033[0;30m"
#define brown  "\033[0;33m"
#define magenta  "\033[0;35m"
#define gray  "\033[0;37m"
#define none   "\033[0m"          /* to flush the previous property */

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

typedef struct  STANDARD_MTBF_TRACE_BODY {
    uint8  receiver_obj;
    uint8  sender_obj;
    uint8  transactoin_id;
    uint8  msg_id;
    uint8  master;
    uint8  channel;
    uint8  time[8];
    uint8  trace_type;
    uint8  trace_id;
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
} STANDARD_MTBF_TRACE_BODY;

typedef struct STANDARD_XXX_TRACE_BODY {
    
    uint8 receiver_obj;
    uint8 sender_obj;
    uint8 transactoin_id;
    uint8 group_id;
    uint8 trace_id;
    uint8 filler;
    uint8 time[8];
    uint8 task[2];
    uint8 ptr[4];

    union {
      HEAP_ALLOC_TAIL         hat;
      HEAP_DEALLOC_TAIL       hdt;
    };
} STANDARD_XXX_TRACE_BODY;

typedef struct  STANDARD_MTBF_TRACE_HEADER {
    uint8 media;
    uint8 receiver_device;
    uint8 sender_device;
    uint8 resource;
    uint8 length[2];
    
}STANDARD_MTBF_TRACE_HEADER;

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
    uint32    tracetype;               /* TODO:the trace get from difference source need to be decoded with difference way */
    uint32    fileindex;               /* use to make all blx files name unique     */   
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
    char skip4;
} META_FORMAT_TIME;

/* for .meta file */
typedef struct META_FORMAT_UNIT {
    META_FORMAT_TIME mft;
    char skip;    
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
   global variables
 **************************************************************************/
extern TRACE_DATE           g_trace_date;
extern BLX_FILE_LIST_NODE * g_bfln_header;
extern HEAP_LINK_NODE     * g_heap_link;

/************************************************************************** 
   functions...
 **************************************************************************/
uint32 char_2_hex(char c);

uint32 strtouint32(char * in);

char * get_file_pattern(char * file_path);
uint32 get_file_index_from_path(char * file_path);
uint32 get_file_lines(char * file_path);

void slinkedlst_free(void);
void slinkedlst_dump(const void * mapped_fptr);
void slinkedlst_insert(char * file_path);

void halloc_info_linkedlst_free(void);
void halloc_info_linkedlst_add(uint32 addr,uint32 size);
uint32 halloc_info_linkedlst_get_size(uint32 addr);

void sort_filelist(char * path);

uint16 decode_timestamp(uint8 * bytestream,char * timestring);

#endif

