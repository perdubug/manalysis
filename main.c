/*
 * ma(memory analyzer) is a Post Analysis command-line utility to get memory usage(heap and block) for S40
 *
 *
 * History:
 *     20/05/2012   Yang Ming  Init version.
 *     28/05/2012   Yang Ming  Add heap_init in meta file
 *                             Start recording csv file from heap_init
 *     31/05/2012   Yang Ming  Support '-ng' and '-b <type>' options
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

#include "ma.h"
#include "thread_pool.h"

/************************************************************************** 
   global variables...
 **************************************************************************/
TRACE_DATE           g_trace_date;
BLX_FILE_LIST_NODE * g_bfln_header;
HEAP_LINK_NODE     * g_heap_link;

/************************************************************************** 
    functions...
 **************************************************************************/

uint8 scan_single_meta_file(FILE * fd_rd, FILE * fd_wr, uint32 init_free_heap,uint8 bCheckHeapInit)
{
    META_FORMAT_UNIT * meta_unit;
    uint32 hour,minute,second;
    uint32 size;

    char line_rd[MAX_SINGLE_METADATA_LEN] = {0};
    char line_wr[MAX_SINGLE_METADATA_LEN] = {0};
    char temp[3]; 

    static uint32 last_hours       = INVALID_HOUR;
    static uint8  find_begin_point = FALSE;
    static uint32 free_heap        = 0;

    uint32 addr;

    if (free_heap == 0) {
        free_heap = init_free_heap;
    }

    while (fgets(line_rd,MAX_SINGLE_METADATA_LEN, fd_rd) != 0) {

        meta_unit = (META_FORMAT_UNIT *)line_rd;

        //meta_unit->skip6   = '\0';
        meta_unit->mft.skip4 = '\0';
        
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

        size  = strtouint32(meta_unit->size);
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
            case TYPE_INIT:
                  find_begin_point = TRUE;
                  continue;
 
            case TYPE_ALLOCATE:
                  halloc_info_linkedlst_add(addr,size);
                  free_heap -= size;
                  sprintf(line_wr,"%02d/%02d/%04d %02d:%02d:%02d.%9s, %08d\n",
                          g_trace_date.day,g_trace_date.month,g_trace_date.year,
                          hour,minute,second,meta_unit->mft.ms,free_heap);
                  break;

            case TYPE_DEALLOCATE:
                  free_heap += halloc_info_linkedlst_get_size(addr);
                  sprintf(line_wr,"%02d/%02d/%04d %02d:%02d:%02d.%9s, %08d\n",
                          g_trace_date.day,g_trace_date.month,g_trace_date.year,
                          hour,minute,second,meta_unit->mft.ms,free_heap);
                  break;

            default:
                  assert(1);
        }

        if (!bCheckHeapInit) {
            fwrite(line_wr,strlen(line_wr),1,fd_wr);
        } else if (find_begin_point) {
            fwrite(line_wr,strlen(line_wr),1,fd_wr);
        }
    }

    return find_begin_point;
}

uint8 build_csv(uint32 init_free_heap, uint8 bCheckHeapInit)
{
    uint8 bret = FALSE;
 
    FILE * fd_meta_list;
    FILE * fd_csv;
    FILE * fd_meta;
    FILE * fd_date;
 
    uint16 len;

    uint8  hasHeapInit = FALSE;

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

        bret = scan_single_meta_file(fd_meta,fd_csv,init_free_heap,bCheckHeapInit);
        hasHeapInit = (bret == TRUE? TRUE : hasHeapInit);

        fclose(fd_meta);

    } /* end while */

    fclose(fd_meta_list);
    fclose(fd_csv);

    /* get the end time */
    gettimeofday(&endTime, NULL);

    if (!hasHeapInit) {
        fprintf(stdout,"Warning: HEA_INIT no found!\n");
    }

    /* calculate time in microseconds */
    wall_clock_counter = (endTime.tv_sec*1000000  + (endTime.tv_usec)) - (startTime.tv_sec*1000000 + (startTime.tv_usec));
    fprintf(stdout,"------------------------------------------------------\n");
    fprintf(stdout,"Time cost:%f minutes\n",wall_clock_counter/(1000000*60));

    return bret;
}

void metadata_single_blx_file(void * arg)
{
    THREAD_PARAMETER * tp = (THREAD_PARAMETER *)arg;

    FILE * blx_file;
    FILE * fd_meta;

    STANDARD_MTBF_TRACE_HEADER smth;
    STANDARD_MTBF_TRACE_BODY   smtb; 
    
    char time_stamp[32] = {0};   

    char metadata[MAX_SINGLE_METADATA_LEN] = {0};
    char meta_file[MAX_PATH_LEN]; 

    char temp[3];
    long length; /* trace item length */

    //fpos_t pos;

    if (tp == NULL) {
        assert(1);
    }

    memset(&smth,0x0,sizeof(STANDARD_MTBF_TRACE_HEADER));
    memset(&smtb,0x0,sizeof(STANDARD_MTBF_TRACE_BODY));

    /* open blx file for reading */
    blx_file = fopen(tp->filepath, "rb");
    if (blx_file == NULL)  {
       fprintf(stderr,"Could not open %s\n",tp->filepath);
       free(tp);
       return;
    }

    /* open meta file for writing */
    sprintf(meta_file,"%s%s.%d%s",DEFAULT_META_FOLDER_PREFIX,basename(tp->filepath),tp->fileindex,DEFAULT_META_FILE_SUFFRIX);
    fd_meta = fopen(meta_file, "a");
    if (fd_meta == NULL)  {
       fclose(blx_file);
       free(tp);
       fprintf(stderr,"Could not create %s\n",meta_file);
       return;
    }

    /* jump fixed header,maybe more bytes I can jump... */
    fseek(blx_file, 0L, SEEK_SET);
    fseek(blx_file, BLX_STARTING_POINT, SEEK_CUR);

    while (!feof(blx_file))   {

        //fgetpos(blx_file, &pos);
        //printf("read 0x%x\n", pos);

        if (fread(&smth, sizeof(smth), 1, blx_file) != 1) {
            break;
        }

        switch (tp->tracetype ) 
        {  
        case TRACE_TYPE_DEFAULT:     /* default type for MTBF trace */
            if ((smth.media != MEDIA_TYPE_TCPIP && smth.media != MEDIA_TYPE_USB) ||
                smth.receiver_device != RECEIVER_DEVICE_PC || 
                smth.sender_device != SEND_DEVICE_TRACEBOX || 
                smth.resource != RESOURCE_TRACEBOX )  {
                
                /* it's not a stand trace iteam at all, remember to back... */ 
                fseek(blx_file, -sizeof(smth)+1L, SEEK_CUR);
                continue; 
            }
 
            /* yes, it is a available trace item... */

            temp[0] = smth.length[0];
            temp[1] = smth.length[1];
            temp[2] = '\0';
            length = strtouint32(temp);

            if (fread(&smtb, sizeof(smtb), 1, blx_file) != 1)  {
                break;
            }

            if (smtb.msg_id != SIGNATURE_MESSAGE_ID ||
                smtb.master != SIGNATURE_MASTER     ||
                smtb.trace_type != SIGNATURE_HEAP_TYPE )  {
              
                /* it's an available trace item but it's not the HEAP trace we are looking for...jump to next trace item */
                fseek(blx_file, -sizeof(smtb)+length, SEEK_CUR);
                continue;
            }
                     
            if (smtb.trace_id != SIGNATURE_HEAP_DEALLOC       && smtb.trace_id != SIGNATURE_HEAP_ALLOC &&
                smtb.trace_id != SIGNATURE_HEAP_ALLOC_NO_WAIT && smtb.trace_id != SIGNATURE_HEAP_INIT &&
                smtb.trace_id != SIGNATURE_HEAP_COND_ALLOC    && smtb.trace_id != SIGNATURE_ALIGNED_ALLOC_NO_WAIT &&
                smtb.trace_id != SIGNATURE_ALIGNED_ALLOC      && smtb.trace_id != SIGNATURE_HEAP_ALLOC_NO_WAIT_FROM )  {
                
                /* it's an available heap trace item, but it's not the ALLOC/DEALLOC/INIT HEAP trace we are looking for
                   ...jump to next trace item */
                fseek(blx_file, -sizeof(smtb)+length, SEEK_CUR);
                continue;
            }
 
            decode_timestamp(&smtb.time[0],time_stamp);
             
            switch (smtb.trace_id)
            {
                case SIGNATURE_HEAP_INIT:
                    sprintf(metadata,META_DATA_FORMAT,time_stamp,TYPE_INIT,0,0,0,0,0);
                    fwrite(metadata,strlen(metadata),1,fd_meta);                
                    break;

                case SIGNATURE_HEAP_DEALLOC:
                    sprintf(metadata,META_DATA_FORMAT,time_stamp,TYPE_DEALLOCATE,GET_PTR(smtb.ptr),0,0,
                            GET_PTR(smtb.hdt.caller1),GET_PTR(smtb.hdt.caller2));
                    fwrite(metadata,strlen(metadata),1,fd_meta);
                    break;

                case SIGNATURE_HEAP_ALLOC:
                    sprintf(metadata,META_DATA_FORMAT,time_stamp,TYPE_ALLOCATE,GET_PTR(smtb.ptr),GET_SIZE(smtb.hat.size),AT_HEAP_ALLOC,
                            GET_PTR(smtb.hat.caller1), GET_PTR(smtb.hat.caller2));
                    fwrite(metadata,strlen(metadata),1,fd_meta);
                    break; 
                case SIGNATURE_HEAP_ALLOC_NO_WAIT:
                    sprintf(metadata,META_DATA_FORMAT,time_stamp,TYPE_ALLOCATE,GET_PTR(smtb.ptr),GET_SIZE(smtb.hat.size),AT_HEAP_ALLOC_NO_WAIT,
                            GET_PTR(smtb.hat.caller1), GET_PTR(smtb.hat.caller2));
                    fwrite(metadata,strlen(metadata),1,fd_meta);
                    break; 
                case SIGNATURE_HEAP_COND_ALLOC:
                    sprintf(metadata,META_DATA_FORMAT,time_stamp,TYPE_ALLOCATE,GET_PTR(smtb.ptr),GET_SIZE(smtb.hat.size),AT_HEAP_COND_ALLOC,
                            GET_PTR(smtb.hcat.caller1), GET_PTR(smtb.hcat.caller2));
                    fwrite(metadata,strlen(metadata),1,fd_meta);
                    break; 
                case SIGNATURE_ALIGNED_ALLOC_NO_WAIT:
                    sprintf(metadata,META_DATA_FORMAT,time_stamp,TYPE_ALLOCATE,GET_PTR(smtb.ptr),GET_SIZE(smtb.hat.size),AT_ALIGNED_ALLOC_NO_WAIT,
                            GET_PTR(smtb.haat.caller1), GET_PTR(smtb.haat.caller2));
                    fwrite(metadata,strlen(metadata),1,fd_meta);
                    break; 
                case SIGNATURE_ALIGNED_ALLOC:
                    sprintf(metadata,META_DATA_FORMAT,time_stamp,TYPE_ALLOCATE,GET_PTR(smtb.ptr),GET_SIZE(smtb.hat.size),AT_ALIGNED_ALLOC,
                            GET_PTR(smtb.haat.caller1), GET_PTR(smtb.haat.caller2));
                    fwrite(metadata,strlen(metadata),1,fd_meta);
                    break; 
                case SIGNATURE_HEAP_ALLOC_NO_WAIT_FROM:
                    sprintf(metadata,META_DATA_FORMAT,time_stamp,TYPE_ALLOCATE,GET_PTR(smtb.ptr),GET_SIZE(smtb.hat.size),AT_ALLOC_NO_WAIT_FROM,
                            GET_PTR(smtb.hanwft.caller1), GET_PTR(smtb.hanwft.caller2));
                    fwrite(metadata,strlen(metadata),1,fd_meta);
                    break; 
                default:
                    break;
            } /* end switch (smtb.trace_id) */

            /* move cursor to a right place */
            fseek(blx_file, -sizeof(smtb)+length, SEEK_CUR);
            break;

        default:
            fseek(blx_file, -sizeof(smth)+1L, SEEK_CUR);
            break;

        } /* end switch (tp->tracetype ) */
    } /* end while */

    fclose(blx_file);
    fclose(fd_meta);
    
    free(tp);

    return;
}


uint8 build_metadata(char * trace_type)
{
    uint8 bret = FALSE;

    FILE * fd_blx_list_file;
    FILE * fd_meta_list_file;
    FILE * fd_date_file;

    char   single_file_path[MAX_PATH_LEN+1] = {0};
    char   meta_file_path[MAX_PATH_LEN+1] = {0};
    char   commandstr[128];

    uint32 t_type = (trace_type == NULL ? 0 : strtouint32(trace_type));
    uint32 fileindex;
    uint32 len,filenums = 0;

    struct timeval startTime;
    struct timeval endTime;

    threadpool tpool;
    
    THREAD_PARAMETER * tp;

    double wall_clock_counter = 0;

    static uint8 has_start_date = FALSE; /* we need an initialization date to cover 120 hours timeline */
    struct stat stbuf;
    struct tm * tm_date;

    system(REMOVE_DEFAULT_META_FOLDER);
    system(CREATE_DEFAULT_META_FOLDER);

    sprintf(commandstr,
            "ls -l --sort=extension $(find $PWD -type f -name '*.blx') | awk '{print $NF}' > %s",TEMPLATE_FILE_LIST);
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

    /* get the current time(wall-clock time)
       - NULL because we don't care about time zone
     */
    gettimeofday(&startTime, NULL);
    fileindex = 0;
    
    tpool = tp_init_threadpool(MAX_NUM_THREADS);

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

        /* add a job with input parameters(single_file_path) into thread pool. The job will handle by metadata_single_blx_file function */
        tp = malloc(sizeof(THREAD_PARAMETER));
        
        tp->tracetype = t_type;
        tp->fileindex = fileindex++;
        strncpy(tp->filepath,single_file_path,MAX_PATH_LEN);

        sprintf(meta_file_path,"%s%s.%d%s\n",DEFAULT_META_FOLDER_PREFIX,basename(single_file_path),tp->fileindex,DEFAULT_META_FILE_SUFFRIX);
        fwrite(meta_file_path,strlen(meta_file_path),1,fd_meta_list_file);

        tp_dispatch(tpool, metadata_single_blx_file, (void *)tp);

        /* Is it necessary here? */
        memset(single_file_path,0x0,MAX_PATH_LEN);

    } /* end-while */

    fclose(fd_blx_list_file);
    fclose(fd_meta_list_file);

    tp_start_threadpool(tpool);

    tp_destroy_threadpool(tpool);
    
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

    //printf("ret=%d\n",ret);
    return ret;
}

void show_usage(void)
{
    fprintf(stdout,"Usage: ma <option>\r\n");
    fprintf(stdout,"Options:\r\n");
    fprintf(stdout,"  Currently, the following options are supported...\r\n");
    fprintf(stdout,"   -b                   build meta data by scanning all blx files recursively and generate metadata at %s\r\n",DEFAULT_META_FOLDER_PREFIX);
    fprintf(stdout,"   -b <type>            same as -b, <type> indicate trace type, default type is MTBF trace,1 mean 11.2 trace \r\n");
    fprintf(stdout,"   -r                   output general heap usage\r\n");
    fprintf(stdout,"   -s <sampling rate>   generate a csv with specified sampling rate\r\n");
    fprintf(stdout,"   -t <minutes>         generate a csv by sampling every <minutes>\r\n");
    fprintf(stdout,"   -g                   generate a completely csv file based on meta files\r\n");
    fprintf(stdout,"   -g <free heap size>  same as -g, but specify init free heap size\r\n");
    fprintf(stdout,"   -ng <init_heap_size>,same as -g, but specify init free heap size, and no need to check heap_init \r\n");
    fprintf(stdout,"   -ng                  same as -g, same as -g, but use default init free heap size,and no need to check heap_init \r\n");
    fprintf(stdout,"\r\n");
}

int main(int argc, char * argv[])
{
    sint32 start_time;
    sint32 end_time;
    uint8 bret = FALSE;

////////////////////////////////////////////////////////////////////////////////////
    switch (argc)
    {
       case 2:  /* --help, -r, -b, -g */
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
                   bret = build_metadata(TRACE_TYPE_DEFAULT);
                   break;

               case 'g':                      /* -g, build big csv based on meta files with default total heap size   */
                   bret = build_csv(DEFAULT_TOTAL_FREE_HEAP,TRUE);
                   break;

               case 'n':                      /* -ng, build big csv based on meta files with default total free heap size,no need to check heap_init  */
                   if (argv[1][2] == 'g')  {
                       bret = build_csv(DEFAULT_TOTAL_FREE_HEAP,FALSE);
                   }
                   break;

               case 'r':                      /* -r, generate general heap report  */
                   //TODO
                   bret = opt_handler_r(argv[2]);   
                   break;


               default: 
                    break;
           }
           break;

       case 3:  /* -s, -t, -b */
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

               case 'b':               /* -b <type>, build blx based on type. Default type is for MTBF trace */
                   bret = build_metadata(argv[2]);
                   break;

               case 'g':               /* -g <init_heap_size> */
                   if (get_expression_result(argv[2]) > 0)  {
                       bret = build_csv(get_expression_result(argv[2]),TRUE);
                   } else {
                       show_usage();
                       return 1;
                   }
                   break;

               case 'n':               /* -ng <init_heap_size>,no need to check heap_init */
                   if (argv[1][2] == 'g' && get_expression_result(argv[2]) > 0)  {
                       bret = build_csv(get_expression_result(argv[2]),FALSE);
                   } else {
                       show_usage();
                       return 1;
                   }
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


