#ifndef TGREP_H
#define TGREP_H

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

// need to include this after _FILE_OFFSET_BITS
#include <stdio.h>

#define LOG_PATH "/logs/haproxy.log"
#define PATH_LEN 200
#define BUFFER_SIZE 5000
#define SEARCH_B_SIZE 1000
#define MIN_PRINT_RANGE 5000LL
#define DISTANCE_SHORTENER 3000LL
#define MAX_LINE_LEN  800

#define POSIX_DATE_REGEX "^[A-Z][a-z]+\\s+([0-9]+\\s+[0-9]+:[0-9]+:[0-9]+)"

typedef struct my_time{
  int	      day; //0 if the first day on the log, 1 if not
  int	      hour;
  int	      min;
  int	      sec;
} my_time;

typedef struct file_time_range{
  const my_time *start_time; // time at the beginning of the file
  const my_time *pre_time1;  // second before time1, the goal of our search
  off_t	avg_sec_size; //average length of a second
  int  start_day;	     // day of the beginning of the file

  const my_time *time1; // start time specified by user
  const my_time *time2; // end time specified by user

  my_time *ct1;		// time at off1
  my_time *ct2;		// time at off2

  off_t off1;
  off_t off2;
} file_time_range;


void search_log(FILE *log_file, my_time *time1, my_time *time2);
int rec_search_file(FILE *log_file, file_time_range *range, regex_t *date_reg, char *buffer);
off_t estimate_distance(file_time_range *range, const my_time *time1);

int print_file_range(FILE *log_file, file_time_range *range, regex_t *date_reg);

int set_range(file_time_range *old_r, file_time_range *new_r, my_time *cur1, my_time *cur2, off_t off1, off_t off2);

int timetoi(const my_time *time1);
int time_diff(const my_time *end_time, const my_time *start_time);
int dec_time(my_time *decd_time, const my_time *orig_time);
void print_my_time(const my_time *time1);
int get_my_times(const char *input, my_time *time1, my_time *time2);
int limit_atoi(char *a, const int high);
void test_estimate_dist();

#endif // TGREP_H
