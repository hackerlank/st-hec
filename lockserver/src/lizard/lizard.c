#include <string.h>
#include <mpi.h>
#include <mpio.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

/* Locking files */
#include "lock.h"
#include "lockserverapi.h"

/*#define USE_COLLECTIVE */
/*#define USE_LISTIO */
/*#define USE_TILE_READS (was USE_BIG_READS) */
/*#define USE_FRAME_READS (was USE_HUGE_READS) */
/*#define USE_SUBTILE_READS (was the default) */

/* NOTE: use subtiles by default.  There are probably better ways to code this
 */
#ifndef USE_TILE_READS
#ifndef USE_FRAME_READS
#ifndef USE_SUBTILE_READS

#define USE_SUBTILE_READS


#endif
#endif
#endif

#define DEBUG

static int g_debug_level = 3;
struct timeval tv_start, tv_stop;

void mpi_clean_up();

void
tiled_movie_3pt_module_debug_print(
    int         level,
    char *      fmt,
    ...);

void report_stats(char *myname,
		  unsigned long tile_sz, 
		  unsigned long num_tiles, 
		  unsigned long num_files);

enum 
{
    TM_SETUP_TIMER=0,
    TM_OPEN_TIMER,
    TM_SEEK_TIMER,
    TM_READ_TIMER,
    TM_CLOSE_TIMER,
};

typedef struct _tiled_movie_timer_s
{
    char label[32];
    float min_time;
    float max_time;
    float total_time;
    float ave_time;
    int count;
    int my_rank;
    int num_procs;
} tiled_movie_timer_t;

struct timeval                           tv_start_all;
struct timeval                           tv_stop_all;
tiled_movie_timer_t                      timer[5];

void init_timer( tiled_movie_timer_t * t, int rank, int num_procs, char * label)          
{
    t->my_rank = rank;
    t->num_procs = num_procs;
    
    strcpy(t->label, label);
}/* init_timer() */

void start_timer( tiled_movie_timer_t * t, int reset)          
{
    if(t->my_rank)
    {
        return;
    }
    if(reset)                                   
    {                                           
        t->min_time = 99999;                     
	t->max_time = 0;                          
	t->count = 0;                      
	t->total_time = 0;                        
	t->ave_time = 0;                         
    }                                           
                                                    
    gettimeofday(&tv_start, 0);                  
}/* start_timer() */

void stop_timer(tiled_movie_timer_t * t)                                                             
{                                                                             
    float time_diff;                                                          
    if(t->my_rank)                                                             
    {                                                                         
        return;                                                               
    }                                                                         
	                                                                          
    t->count++;                                                          
    gettimeofday(&tv_stop, 0);                                                
    time_diff =                                                               
      (((double) tv_stop.tv_sec) + ((double) tv_stop.tv_usec) / 1000000.0) -    
      (((double) tv_start.tv_sec) + ((double) tv_start.tv_usec) / 1000000.0) ;  
                                                                                  
    if(time_diff < t->min_time)                                                
    {                                                                         
        t->min_time = time_diff;                                               
    }                                                                         
    if(time_diff > t->max_time)                                                
    {                                                                         
        t->max_time = time_diff;                                               
    }                                                                         
    t->total_time += time_diff;                                                
    t->ave_time = t->total_time/(float)t->count;                           
}/* stop_timer() */


void print_timer(tiled_movie_timer_t * t)
{
    static char * myname = "tiled_movie_timing";                

    if(t->my_rank)
    {                                                                         
      return;                                                               
    }                                                                         
    tiled_movie_3pt_module_debug_print(                                       
	1,                                                                
	"[%s]: [%s]: number of readers: %d\n",                      
	myname,
	t->label,
	t->num_procs);                                                     
    tiled_movie_3pt_module_debug_print(                                       
	1,                                                                
	"[%s]: [%s]: minimum time: %f\n",     
	myname,                                                           
	t->label,
	t->min_time);
    tiled_movie_3pt_module_debug_print(                                       
	1,                                                                
	"[%s]: [%s]: maximum time: %f\n",                                  
	myname,                                                           
	t->label,
	t->max_time);                                                      
    tiled_movie_3pt_module_debug_print(                                       
	1,                                                                
	"[%s]: [%s]: average time: %f\n",                                  
	myname,                                                           
	t->label,
	t->ave_time);
    tiled_movie_3pt_module_debug_print(                                       
	1,                                                                
	"[%s]: [%s]: total time: %f\n",                                  
	myname,                                                           
	t->label,
	t->total_time);
}/* print_timer() */

int
tiled_movie_3pt_get_header_info(
    char*                                    filename,
    MPI_Offset                               *numBytes,
    int*                                     xsize,
    int*                                     ysize) 
{
    int                                      returnVal=0;
    char                                     buffer[1024];
    char                                     line_buff[256];
    int                                      i;
    int                                      rc;
    char *                                   p;
    char *                                   p2;
    MPI_File                                 fh;
    MPI_Status                               status;
    static char *                            myname =
	                                     "tiled_movie_3pt_get_header_info";

    *numBytes=0;

    rc = MPI_File_open(MPI_COMM_SELF,
		       filename,
		       MPI_MODE_RDONLY,
		       MPI_INFO_NULL,
		       &fh);

    if(rc != 0) 
    {
	tiled_movie_3pt_module_debug_print(
	    1,
	    "[%s]: could not open file: %s\n",
	    myname,
	    filename);
	
	returnVal = 1;
    }
    /* read the beginning of the file; the header is in here and varies
     * in size.
     */
    rc = MPI_File_read(fh,
		       buffer,
		       1024,
		       MPI_CHAR,
		       &status);

    MPI_File_close(&fh);
    p2=buffer;
    for(i=0; i<3; i++) 
    {
        p=strchr(p2, '\n');
	if(p !=NULL)
	{	    
	    *p = '\0';
	}
	else
	{
	    returnVal = 1;
	    printf("[%s]: in loop %d, failed to find the newline\n", myname, i);
	    break;
	}
	strcpy(line_buff, p2);
	p2 = p+1;
	/* sscanf(p, "%s", line_buff); */
	*numBytes+=strlen(line_buff)+1;
	/*	printf("\'%s\', numBytes = %d\n", line_buff, *numBytes); */
	if (i==1) 
	{
	    sscanf(line_buff, "%d %d", xsize, ysize);
	      tiled_movie_3pt_module_debug_print(
		  5,
		  "[%s]: x: %d, y: %d.\n",
		  myname,
		  *xsize,
		  *ysize);
	} /* if(i==1) */
	/* p=p2+1; */
    } /*  for(i=0; i<3; i++) */

    return returnVal;
}	  


int
main(int argc, char **argv)
{
    MPI_File                                 fh;
    int                                      rc;
    MPI_Datatype *                           filetype;
    MPI_Status                               status;
    int                                      nbytes;
    unsigned char *                          buffer;
    int                                      my_rank;
    int                                      num_procs;
    int                                      full_size[2];
    //int                                      tile_size[2];
    int                                      offset_x[]={0, 781, 1508, 0, 781, 1508};
    int                                      offset_y[]={0, 0, 0, 639, 639, 639};
    int                                      my_offset[2];
    int                                      my_sub_size[2];
    int                                      buffer_size;
    MPI_Datatype                             rgbtriplet;
    MPI_Info                                 info;
    char                                     info_value[16];
    MPI_Offset                               file_header_size;
    char *                                   filename;
    char *                                   file_ext;
    int start_frame;
    int stop_frame;
    int this_frame_num;
    int frame_inc = 1;
    int frame_in_movie_num = 0 ;
    char* this_file;
    int                                      num_files=1;
    int                                      num_tiles=1;
    int                                      tile_loop;
    //int                                      file_loop;
    int                                      timer_reset_flag[5];
    static char *                            myname =
                                        	"MPI_TIMING_TEST";

    /* Lock server stuff */
    char *lockservername = NULL;
    int lockserverport = -1;
    int locktype = -1;
    int lockid = -1;
    int filename_len = -1;
    int file_ext_len = -1;
    int lockservername_len = -1;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    init_timer(&timer[TM_SETUP_TIMER], my_rank, num_procs, "TM_SETUP_TIMER");
    timer_reset_flag[TM_SETUP_TIMER] = 1;
    init_timer(&timer[TM_OPEN_TIMER], my_rank, num_procs, "TM_OPEN_TIMER");
    timer_reset_flag[TM_OPEN_TIMER] = 1;
    init_timer(&timer[TM_SEEK_TIMER], my_rank, num_procs, "TM_SEEK_TIMER");
    timer_reset_flag[TM_SEEK_TIMER] = 1;
    init_timer(&timer[TM_READ_TIMER], my_rank, num_procs, "TM_READ_TIMER");
    timer_reset_flag[TM_READ_TIMER] = 1;
    init_timer(&timer[TM_CLOSE_TIMER], my_rank, num_procs, "TM_CLOSE_TIMER");
    timer_reset_flag[TM_CLOSE_TIMER] = 1;

    /* Process args */
    if (my_rank == 0) {
        start_frame = atoi(argv[3]);
        stop_frame = atoi(argv[4]);
        num_tiles = atoi(argv[5]);
        lockserverport = atoi(argv[7]);
        locktype = atoi(argv[8]);

        filename_len = strlen(argv[1]);
        file_ext_len = strlen(argv[2]);
        lockservername_len = strlen(argv[6]);

        filename = malloc(filename_len+1);
        file_ext = malloc(file_ext_len+1);
        lockservername = malloc(lockservername_len+1);

        strcpy(filename, argv[1]);
        strcpy(file_ext, argv[2]);
        strcpy(lockservername, argv[6]);

        MPI_Bcast(&filename_len, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(filename, filename_len+1, MPI_CHAR, 0, MPI_COMM_WORLD);

        MPI_Bcast(&file_ext_len, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(file_ext, file_ext_len+1, MPI_CHAR, 0, MPI_COMM_WORLD);

        MPI_Bcast(&lockservername_len, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(lockservername, lockservername_len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
    } else {
        MPI_Bcast(&filename_len, 1, MPI_INT, 0, MPI_COMM_WORLD);
        filename = malloc(filename_len+1);
        MPI_Bcast(filename, filename_len+1, MPI_CHAR, 0, MPI_COMM_WORLD);

        MPI_Bcast(&file_ext_len, 1, MPI_INT, 0, MPI_COMM_WORLD);
        file_ext = malloc(file_ext_len+1);
        MPI_Bcast(file_ext, file_ext_len+1, MPI_CHAR, 0, MPI_COMM_WORLD);

        MPI_Bcast(&lockservername_len, 1, MPI_INT, 0, MPI_COMM_WORLD);
        lockservername = malloc(lockservername_len+1);
        MPI_Bcast(lockservername, lockservername_len+1, MPI_CHAR, 0,
                  MPI_COMM_WORLD);
    }

    MPI_Bcast(&start_frame, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&stop_frame, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&num_tiles, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&lockserverport, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&locktype, 1, MPI_INT, 0, MPI_COMM_WORLD);

    /*printf("proc %d: filename='%s'\n", my_rank, filename);
    printf("proc %d: file_ext='%s'\n", my_rank, file_ext);
    printf("proc %d: lockservername='%s'\n", my_rank, lockservername);
    printf("proc %d: lockserverport=%d\n", my_rank, lockserverport);
    printf("proc %d: locktype=%d\n", my_rank, locktype);
    printf("proc %d: start_frame=%d\n", my_rank, start_frame);
    printf("proc %d: stop_frame=%d\n", my_rank, stop_frame);
    printf("proc %d: num_tiles=%d\n", my_rank, num_tiles);*/

    if(start_frame > stop_frame)
    {
        frame_inc = -1;
	num_files = start_frame - stop_frame +1;
    }
    else
    {
        num_files = stop_frame - start_frame +1;
    }
 
#ifdef DEBUG
    tiled_movie_3pt_module_debug_print(
	3,
	"[%s]: ENTER. num_procs= %d, my_rank=%d\n",
	myname,
	num_procs,
	my_rank);
#endif

    this_file = malloc(strlen(filename)+4+strlen(file_ext)+1);
    sprintf(this_file, "%s%04d%s",filename, start_frame, file_ext);
    //printf("proc %d: this_file='%s'\n", my_rank, this_file);

    if (my_rank == 0) {
        rc = tiled_movie_3pt_get_header_info(
	    this_file,
	    &file_header_size,
	    &full_size[1],
	    &full_size[0]);
        if (rc != 0) {
	    tiled_movie_3pt_module_debug_print(
		3,
		"[%s]: could not open file: %s\n",
		myname,
		filename);
	    mpi_clean_up();
        }
    }
    MPI_Bcast(&file_header_size, 1, MPI_LONG_LONG_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(full_size, 2, MPI_INT, 0, MPI_COMM_WORLD);

    //printf("proc %d: file_header_size=%lld\n", my_rank, file_header_size);
    //printf("proc %d: full_size=[%d][%d]\n", my_rank, full_size[0], full_size[1]);

    start_timer(&timer[TM_SETUP_TIMER], timer_reset_flag[TM_SETUP_TIMER]);
    timer_reset_flag[TM_SETUP_TIMER] = 0;

    MPI_Info_create(&info);
    sprintf(info_value, "%d", num_procs);
#ifdef USE_COLLECTIVE
    MPI_Info_set(info, "cb_nodes", info_value);
    MPI_Info_set(info, "romio_cb_read", "enable");
#endif
#ifdef USE_LISTIO
    MPI_Info_set(info, "romio_pvfs_dtype_read", "disable");
    MPI_Info_set(info, "romio_pvfs_listio_read", "enable");
    MPI_Info_set(info, "romio_cb_read", "disable");
#endif
#ifdef USE_DTYPE
    MPI_Info_set(info, "romio_pvfs_dtype_read", "enable");
    MPI_Info_set(info, "romio_pvfs_listio_read", "disable");
    MPI_Info_set(info, "romio_cb_read", "disable");
#endif
#ifndef USE_COLLECTIVE
#ifndef USE_LISTIO
#ifndef USE_DTYPE
    MPI_Info_set(info, "romio_pvfs_dtype_read", "disable");
    MPI_Info_set(info, "romio_pvfs_listio_read", "disable");
    MPI_Info_set(info, "romio_cb_read", "disable");
    MPI_Info_set(info, "romio_ds_read", "enable");
#endif
#endif
#endif

    /* set the individual read buffer size up; this keeps the datasieving
     * reading one big block instead of a few smaller ones.
     *
     * my testing on chiba compute nodes didn't show any real benefit from
     * this in practice, but it definitely does cut the # of PVFS operations
     * down.
     */
    sprintf(info_value, "%d", full_size[0] * full_size[1] * 3);
    MPI_Info_set(info, "ind_rd_buffer_size", info_value);

    MPI_Type_contiguous(3, MPI_BYTE, &rgbtriplet);
    MPI_Type_commit(&rgbtriplet);
    
    stop_timer(&timer[TM_SETUP_TIMER]);
  
    my_sub_size[1] = 1024;
#ifdef USE_TILE_READS
    my_sub_size[0] = 768;
#else
    my_sub_size[0] = 768 / num_procs;

    /* if i'm the bottom chunk of the tile, pick up any extra rows */
    /* NOTE: this could be more optimal, but i doubt it matters */
    if(my_rank == num_procs-1)
    {
	my_sub_size[0] += 768 % num_procs;
    }
#endif
    /* create the MPI subarray's for each tile */
    filetype = (MPI_Datatype *) malloc(sizeof(MPI_Datatype) * num_tiles);

    /* NOTE: I bet there's a way to use one of these types and read_at() to 
     * get the data that we want.  it would save some time in the ROMIO
     * code because it would only need to flatten one datatype.
     */
    for(tile_loop=0; tile_loop < num_tiles; tile_loop++) 
    {
        /* NOTE: the offset_x and offset_y values are hardcoded above */ 
	my_offset[1] = offset_x[tile_loop];
#ifdef USE_TILE_READS
	my_offset[0] = offset_y[tile_loop];
#else
	my_offset[0] = my_sub_size[0] * my_rank + offset_y[tile_loop];
#endif	
	start_timer(&timer[TM_SETUP_TIMER], timer_reset_flag[TM_SETUP_TIMER]);
	timer_reset_flag[TM_SETUP_TIMER] = 0;
 
	MPI_Type_create_subarray(
	    2,
	    full_size,
	    my_sub_size,
	    my_offset,
	    MPI_ORDER_C,
	    rgbtriplet,
	    &filetype[tile_loop]);
	
	MPI_Type_commit(&filetype[tile_loop]);
	
	stop_timer(&timer[TM_SETUP_TIMER]);
    } /* for(tile_loop=0; tile_loop<num_tiles; tile_loop++) */
    
#ifndef USE_FRAME_READS    
    buffer_size = my_sub_size[0] * my_sub_size[1] * 3;
#else
    buffer_size = full_size[0] * full_size[1] * 3;
#endif
    gettimeofday(&tv_start_all, 0);
 
    
    timer_reset_flag[TM_OPEN_TIMER] = 1;
    
    /*  for(file_loop=0; file_loop<num_files; file_loop++) */
#ifdef USE_FRAME_READS
    frame_in_movie_num = my_rank;
    for(this_frame_num=start_frame+(my_rank*frame_inc);
        frame_in_movie_num < num_files;
        this_frame_num+=(num_procs*frame_inc),  frame_in_movie_num+=num_procs)
#else
    for(this_frame_num=start_frame;
        this_frame_num<=stop_frame;
        this_frame_num++)
#endif
    {
	sprintf(this_file, "%s%04d.pnm", filename, this_frame_num);
#ifdef DEBUG
	tiled_movie_3pt_module_debug_print(
	    2,
	    "[%s]:  frame_in_movie_num: %d, getting file: %s\n",
	    myname,
	    frame_in_movie_num,
	    this_file);
#endif
	start_timer(&timer[TM_OPEN_TIMER], timer_reset_flag[TM_OPEN_TIMER]);
	timer_reset_flag[TM_OPEN_TIMER] = 0;
	{

#ifdef USE_COLLECTIVE
	rc = MPI_File_open(
	    MPI_COMM_WORLD,
	    this_file,
	    MPI_MODE_RDONLY,
	    info,
	    &fh);
#endif
#ifdef USE_LISTIO
	rc = MPI_File_open(
	    MPI_COMM_WORLD,
	    this_file,
	    MPI_MODE_RDONLY,
	    info,
	    &fh);
#endif
#ifdef USE_DTYPE
	rc = MPI_File_open(
	    MPI_COMM_WORLD,
	    this_file,
	    MPI_MODE_RDONLY,
	    info,
	    &fh);
#endif
#ifndef USE_COLLECTIVE
#ifndef USE_LISTIO
#ifndef USE_DTYPE
	rc = MPI_File_open(
	    MPI_COMM_SELF,
	    this_file,
	    MPI_MODE_RDONLY,
	    info,
	    &fh);
#endif
#endif
#endif
	}
	stop_timer(&timer[TM_OPEN_TIMER]);

	if(rc != 0)
	{
	    tiled_movie_3pt_module_debug_print(
		2,
		"[%s]: MPI_File_open failed to open file: %s\n",
		myname,
		filename);
	    
	    return 1;
	}
#ifndef USE_FRAME_READS
#ifndef USE_TILE_READS
	for(tile_loop=0; tile_loop<num_tiles; tile_loop++)
	{
#else
	  tile_loop = my_rank;
#endif /* #ifndef USE_TILE_READS */
#endif /* #ifndef USE_FRAME_READS */

	    start_timer(&timer[TM_SEEK_TIMER], timer_reset_flag[TM_SEEK_TIMER]);
	    timer_reset_flag[TM_SEEK_TIMER] = 0;  
	    {  
#ifndef USE_FRAME_READS
	    MPI_File_set_view(
		fh,
		file_header_size,
		rgbtriplet,
		filetype[tile_loop],
		"native",
		info);
#endif
	    }
	    stop_timer(&timer[TM_SEEK_TIMER]);

	    buffer = (unsigned char *)malloc(buffer_size);
	    if(buffer == NULL)
	    {
		tiled_movie_3pt_module_debug_print(
		    3,
		    "[%s]: failed to malloc buffer of size = %d\n",
		    myname,
		    buffer_size);
		
		exit(1);
	    } 

	    start_timer(&timer[TM_READ_TIMER], timer_reset_flag[TM_READ_TIMER]);
	    timer_reset_flag[TM_READ_TIMER] = 0;

            if (locktype != NO_LOCK) {
                lockid = lock_datatype(locktype,
                                       my_sub_size[0] * my_sub_size[1],
                                       rgbtriplet,
                                       file_header_size, 0,
                                       filetype[tile_loop],
                                       rgbtriplet, filename, lockservername,
                                       lockserverport);
            }
#if 0
#ifdef USE_COLLECTIVE
	    /* both tile and subtile collective cases */
	    rc = MPI_File_read_all(
		fh,
		buffer,
		my_sub_size[0] * my_sub_size[1],
		rgbtriplet,
		&status);
#else
#ifndef USE_FRAME_READS	   
	    rc = MPI_File_read(
		fh,
		buffer,
		my_sub_size[0] * my_sub_size[1],
		rgbtriplet,
		&status);
#else
	    MPI_File_seek(fh, file_header_size, MPI_SEEK_SET);
	    rc = MPI_File_read(
		fh,
		buffer,
		buffer_size,
		MPI_CHAR,
		&status);
#endif /* #ifndef USE_FRAME_READS */
#endif /* USE_COLLECTIVE */
#endif
            if (locktype != NO_LOCK) {
                releaseLocks(lockservername, lockserverport, filename, lockid);
            }

	    stop_timer(&timer[TM_READ_TIMER]);

	    if(MPI_SUCCESS != rc)
	    {
		printf("BAD RETURN THINGY\n");
		exit(1);
	    }
	    nbytes = 0;
    
	    rc = MPI_Get_count(&status, rgbtriplet, &nbytes);
	    if(MPI_SUCCESS != rc)
	    {
		printf("BAD RETURN THINGY\n");
		exit(1);
	    }
	    
	    
	    if(nbytes != (buffer_size/3))
	    {
		tiled_movie_3pt_module_debug_print(
		    1,
		    "[%s]: NOT the same:  my_sub_size[0]= %d, my_sub_size[1]= %d, nbytes= %d, buffer_size: %d\n",
		    myname,
		    my_sub_size[0],
		    my_sub_size[1],
		    nbytes,
		    buffer_size);
	    }
	    else
	    {
		tiled_movie_3pt_module_debug_print(
		    4,
		    "[%s]: nbytes= %d, buffer_size/3: %d\n",
		    myname,
		    nbytes,
		    buffer_size/3);
	    }

	    free(buffer);
	    
#ifndef USE_FRAME_READS
#ifndef USE_TILE_READS
	} /* for(tile_loop=0; tile_loop<num_tiles; tile_loop++) */
#endif
#endif

	start_timer(&timer[TM_CLOSE_TIMER], timer_reset_flag[TM_CLOSE_TIMER]);
	timer_reset_flag[TM_CLOSE_TIMER] = 0;

	MPI_File_close(&fh);

	stop_timer(&timer[TM_CLOSE_TIMER]);


    } /* for(file_loop=0; file_loop<num_files; file_loop++) */


    MPI_Barrier(MPI_COMM_WORLD);
    gettimeofday(&tv_stop_all, 0);

    /* print out statistics */
#ifdef USE_FRAME_READS
    if (my_rank == 0) 
    {
	report_stats(
	    myname, 
	    full_size[0]*full_size[1]*3,
	    1,
	    num_files
	    );
    }
#else
    if (my_rank == 0) 
    {
	report_stats(
	    myname, 
	    1024*768*3,
	    num_tiles,
	    num_files
	    );
    }
#endif

    mpi_clean_up();
    exit(0);
} /* main */

/* call only for rank 0
 *
 * for frame reads, tile_sz = frame_sz, num_tiles = 1, num_files = N
 * (consider the frame as one big tile)
 *
 * for tile reads, all the values are pretty straightforward.
 */
void report_stats(char *myname,
		  unsigned long tile_sz, 
		  unsigned long num_tiles, 
		  unsigned long num_files)
{
    int loop;
    float total_mpi_time = 0.0, time_diff;
    double mbits;

    time_diff = (((double) tv_stop_all.tv_sec) + 
		 ((double) tv_stop_all.tv_usec) / 1000000.0) -
	        (((double) tv_start_all.tv_sec) + 
	         ((double) tv_start_all.tv_usec) / 1000000.0);


    for(loop=0; loop< 5; loop++)
    {
	print_timer(&timer[loop]);
	total_mpi_time += timer[loop].total_time;
    }
    
    tiled_movie_3pt_module_debug_print(
	1,
	"[%s]: Total mpi io time: %f\n",
	myname,
	total_mpi_time);

    mbits = (((double)(tile_sz * num_tiles * num_files)) / (double) 1048576.0) 
	     *8.0;
    
    tiled_movie_3pt_module_debug_print(
	2,
	"[%s]: total_time: %f Mbits: %f  io rate: %f Mbits/sec\n",
	myname,
	time_diff,
	(float) mbits,
	(float) mbits / time_diff);

    tiled_movie_3pt_module_debug_print(
	3,
	"[%s]: END, success\n",
	myname);
    
}

 
void
mpi_clean_up()
{
    int myid;
    MPI_Comm_rank(MPI_COMM_WORLD, &myid);
    printf( "Cleaning UP %d\n", myid);
    
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    exit(0);
}



void
tiled_movie_3pt_module_debug_print(
    int                                       level,
    char *                                    fmt,
    ...)
{
    va_list                                   ap;
    pid_t                                     pid;

    pid = getpid();

    if(level <= g_debug_level)
    {
        va_start(ap, fmt);
        
        {
            fprintf(stdout, "p%d:", pid);
            vfprintf(stdout, fmt, ap);
            fflush(stdout);
        }
        va_end(ap);

    }
} /* tiled_movie_3pt_module_debug_print() */




