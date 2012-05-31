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

#include "ma.h"

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
         //TODO: a reset is here due to bfiof is NULL when sorting some file list....
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

