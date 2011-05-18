#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include "tgrep.h"

//
// Global Variables
//

// truncate end says to truncate the end of the input range if it is past the end of the file
int truncate_end;

// truncate start says to truncate the start of the input range if it is before the start of the file
int truncate_start;

// if 1 does a strict binary search rather than a best guess search
int strict_binary_search;

int main(int argc, char *argv[])
{
  FILE	      *log_file = NULL;
  int	      read_times = 0;

  my_time     time1;
  my_time     time2;

  //global options
  truncate_end = 0;
  truncate_start = 0;
  strict_binary_search = 0;

  //
  // Parse Args
  //
  for(int carg = 1; carg < argc; carg++){
    if(NULL == log_file && (log_file = fopen(argv[carg], "r")))
      continue;

    if(0 == read_times && (read_times = get_my_times(argv[carg], &time1, &time2)))
      continue;

    if((strlen(argv[carg]) == 2) && (0 == strncmp(argv[carg], "-s",2))){
      truncate_start = 1;
      continue;
    }

    if((strlen(argv[carg]) == 2) && (0 == strncmp(argv[carg], "-e",2))){
      truncate_end = 1;
      continue;
    }

    if((strlen(argv[carg]) == 2) && (0 == strncmp(argv[carg], "-b",2))){
      strict_binary_search = 1;
      continue;
    }

    // If times have been read or it's not a time, and it's not a valid file, exit
    if(NULL == log_file)
      fprintf(stderr, "%s couldn't be opened for reading or is a bad time range\n", argv[carg]);
    else if(read_times) 
      fprintf(stderr, "you put in too many arguments!\n");
    else
      fprintf(stderr, "invalid time argument!\n");

    exit(0);
  }

  // Open the default log file if "log_file" is still NULL
  if(log_file == NULL && NULL == (log_file = fopen(LOG_PATH, "r"))){
    fprintf(stderr, LOG_PATH " couldn't be opened for reading\n");
    exit(0);
  }

  search_log(log_file, &time1, &time2);
}

/* Get's start time and end time of the file.
   Set's up params for a call to rec_search_file  */
void search_log(FILE *log_file, my_time *time1, my_time *time2)
{/*{{{*/
  struct stat	    log_stat;
  off_t		    log_size;
  char		    buffer[SEARCH_B_SIZE+1];

  regex_t	    date_reg;
  regmatch_t	    date_match[2];
  int		    reg_err;

  my_time	    start_time;	    // time of first log in file
  my_time	    end_time;	    // time of last log in file
  my_time	    pre_time1;  // second before time1 (goal of the search)
  int		    start_day;	    // day of month of first log in file
  int		    end_day;

  file_time_range   range;

  memset(&start_time, 0, sizeof(my_time));
  memset(&end_time, 0, sizeof(my_time));

  // make a date matching regex
  // my first ever regex in c!
  if(regcomp(&date_reg, POSIX_DATE_REGEX, REG_EXTENDED | REG_NEWLINE)){
    fprintf(stderr, "regex failed to compile\n");
    exit(0);
  }

  // get start_time (time of first log in file)
  if(0 >= fread(buffer, 1, SEARCH_B_SIZE, log_file)){
    fprintf(stderr, "Error reading from file");
    return;
  } else buffer[SEARCH_B_SIZE] = '\0';
  
  reg_err = regexec(&date_reg, buffer, 2, date_match, 0);
  if(reg_err == REG_NOMATCH)
    printf("REGEX! Y U NO MATCH\n");
  else if(reg_err)
    printf("forever alone\n");
  else{
    if(4 != sscanf(buffer + date_match[1].rm_so, "%d%d:%d:%d", &start_day, &(start_time.hour), &(start_time.min), &(start_time.sec)))
      fprintf(stderr, "format error in file");
    start_time.day = 0;
  }

  // get end_time by matching everything after 300 bytes until the end.
  fseeko(log_file, -300LL, SEEK_END);
  fread(buffer, 1, SEARCH_B_SIZE, log_file);
  buffer[300] = '\0';

  char *buf_ptr = buffer;
  while(1){
    reg_err = regexec(&date_reg, buf_ptr, 2, date_match, 0);
    if(reg_err == REG_NOMATCH){
      break;
    }
    else if(reg_err){
      fprintf(stderr, "some regex_error occurred: %d\n", reg_err);
      goto search_log_freedom;
    }
    else{
      sscanf(buf_ptr + date_match[1].rm_so, "%d%d:%d:%d", &end_day, &(end_time.hour), &(end_time.min), &(end_time.sec));
      if(end_day != start_day)
	end_time.day = 1;
      else
	end_time.day = 0;
    }
    buf_ptr += date_match[1].rm_eo; // go past the current match;
  }
  log_size = ftello(log_file);

  // infer the days of the user input times
  time1->day = 0;
  if(time_diff(time1, &start_time) < 0)
    time1->day = 1;

  time2->day = 0;
  if(time_diff(time2, time1) < 0)
    time2->day = 1;
  if(time_diff(time2, &end_time) > 0){
    if(truncate_end){
      time2 = &end_time;
    }
    else if(truncate_start){
      time1 = &start_time;
      time2->day = 0;
    }
    else{
      fprintf(stderr,"you picked a weird time that overlaps the beginning and the end of the time range\n");
      fprintf(stderr,"\t(your end time seems to be closer to the start than you're start time)\n");
      goto search_log_freedom;
    }
  }
  dec_time(&pre_time1, time1); // yes, this could theoretically be before start_time

  // initialize range, which will pass data to recursive calls
  range.start_time = &start_time;
  range.pre_time1 = &pre_time1;
  range.time1 = time1;
  range.time2 = time2;
  range.start_day = start_day;
  range.avg_sec_size = log_size / (off_t)time_diff(&end_time, &start_time);

  set_range(&range, &range, &start_time, &end_time, 0L, log_size);

  //
  // The hunt is on
  //
  rec_search_file(log_file, &range, &date_reg, buffer);

search_log_freedom:
  regfree(&date_reg);
}/*}}}*/

/*  Estimates the location, goes to it, calls self
    base case: seems to be close enough to the file

    Assumes we are currently at range->off1
				range->ct1

    pass in buffer to avoid excess memory allocation
    probably should have just made it a loop instead  */
int rec_search_file(FILE *log_file, file_time_range *range, regex_t *date_reg, char *buffer)
{/*{{{*/
  my_time	    cur_time;
  int		    cur_day;
  off_t		    cur_off;
  off_t		    min_dist = (range->avg_sec_size / 2 > MIN_PRINT_RANGE) ? range->avg_sec_size/2 : MIN_PRINT_RANGE;
  regmatch_t	    date_match[2];

  off_t		    est_dist;

  // find out where to seek to
  if(strict_binary_search)
    cur_off = range->off1/2 + range->off2/2;
  else
    cur_off = range->off1 + estimate_distance(range, range->pre_time1);

  // if we are jumping by a trivial amount, just bump it up a bit
  if(0 < cur_off - range->off1 && cur_off - range->off1 < min_dist && range->off2 - range->off1 > 2 * min_dist)
    cur_off = range->off1 + min_dist;
  if(0 > cur_off - range->off1 && cur_off - range->off1 > min_dist && range->off2 - range->off1 > 2 * min_dist)
    cur_off = range->off1 - min_dist;

  // might happen if you're searching for the beginning of a file
  if(cur_off < 0) cur_off = 0;

  fseeko(log_file, cur_off, SEEK_SET);

  // get time where we are
  if(0 >= fread(buffer, 1, MAX_LINE_LEN, log_file)){
    if(feof(log_file))
      fprintf(stderr, "hit eof at offset %lld\n", cur_off);
    else
      fprintf(stderr, "read error!\n");
    return 1;
  } else buffer[MAX_LINE_LEN] = '\0';

  int reg_err = regexec(date_reg, buffer, 2, date_match, 0);
  if(reg_err == REG_NOMATCH){
    fprintf(stderr, "REGEX! Y U NO MATCH?\n");
    return 1;
  }
  else if(reg_err){
    printf("forever alone\n");
    return 1;
  }
  else{
    sscanf(buffer + date_match[1].rm_so, "%d%d:%d:%d", &cur_day, &(cur_time.hour), &(cur_time.min), &(cur_time.sec));
    cur_time.day = (cur_day == range->start_day) ? 0 : 1;
  }
  
  // decide which params to pass to the next recursive call
  file_time_range new_range;
  // if cur_time is ahead of time1, then we make it our new ct2
  // otherwise we make it our new ct1
  if(time_diff(&cur_time, range->time1) >= 0)
    set_range(range, &new_range, range->ct1, &cur_time, range->off1, cur_off);
  else
    set_range(range, &new_range, &cur_time, range->ct2, cur_off, range->off2);

  // REAL BASE CASE!
  // if the estimated distance is greater than 0 (we have to be before the time1)
  // and it's less than the minimum print range, then print
  // - the only case where we accept an est_dist of 0 is at the beginning of the file
  est_dist = estimate_distance(&new_range, range->time1);

  if(((0 < est_dist) && (est_dist < MIN_PRINT_RANGE)) ||
      ((0 == est_dist) && (0 == new_range.off1))){
    fseeko(log_file, new_range.off1, SEEK_SET);
    print_file_range(log_file, &new_range, date_reg);
    return 0;
  }

  // recurse
  return rec_search_file(log_file, &new_range, date_reg, buffer);
}/*}}}*/

/* To be called once we've found somewhere close enough to (but never past) the start time
   will do a linear search to find the start time, and then print everything from there to the end

   pass in the regex used for the dates, the file_time_range search params, and the log file.
   returns 0 on success, 1 on error							      */
int print_file_range(FILE *log_file, file_time_range *range, regex_t *date_reg)
{/*{{{*/
  char		    buffer[BUFFER_SIZE+1];
  char		    *start_ptr = NULL;

  my_time	    cur_time;
  int		    cur_day;
  char		    *buf_ptr;

  int		    end_not_found = 1;

  int		    reg_err;
  regmatch_t	    date_match[2];
  
  // find the point that the first line we care about starts at
  // by searching regex match by regex match
  while(!start_ptr){
    if(0 >= fread(buffer, 1, BUFFER_SIZE, log_file)){
      if(feof(log_file)){
	fprintf(stderr, "reached eof while searching for first date?? Talk to Thomas.\n");
	break;
      }
      break;
    } else buffer[BUFFER_SIZE] = '\0';

    buf_ptr = buffer;
    while(1){
      // keep matching until we find the first match
      reg_err = regexec(date_reg, buf_ptr, 2, date_match, 0);
      if(reg_err == REG_NOMATCH)
	break;
      else if(reg_err)
	printf("forever alone\n");
      else{
	sscanf(buf_ptr + date_match[1].rm_so, "%d%d:%d:%d", &cur_day, &(cur_time.hour), &(cur_time.min), &(cur_time.sec));
	cur_time.day = (cur_day == range->start_day) ? 0 : 1;

	if(time_diff(&cur_time, range->time1) >= 0){
	  start_ptr = buf_ptr + date_match[0].rm_so;
	  break;
	}
	else
	  buf_ptr += date_match[0].rm_eo;
      }
    }
  }

  // by now we have the start time, so find the end time
  // if the end time is in the current buffer, then put a '\0' after it
  // print the whole buffer
  while(end_not_found){
    // estimate whether the end is in this buffer.  If it is clearly past, check the end of the buffer
    off_t estimate = estimate_distance(range, range->time2);

    // check the end of the buffer to see if it is still in the time range
    // if it is, skip to printing the buffer out
    if(estimate > BUFFER_SIZE){
      buf_ptr = buffer + BUFFER_SIZE - MAX_LINE_LEN;
      while(1){
	reg_err = regexec(date_reg, buf_ptr, 2, date_match, 0);
	if(reg_err == REG_NOMATCH)
	  break;
	else if(reg_err)
	  printf("forever alone\n");
	else{
	  sscanf(buf_ptr + date_match[1].rm_so, "%d%d:%d:%d", &cur_day, &(cur_time.hour), &(cur_time.min), &(cur_time.sec));
	  cur_time.day = (cur_day == range->start_day) ? 0 : 1;

	  // if somewhere in this buffer there's a time past the end of the time range, just use linear search
	  if(time_diff(&cur_time, range->time2) > 0){ 
	    break;
	  }
	  else
	    buf_ptr += date_match[0].rm_eo;
	}
      } 

      // we now either have the last time in the buffer loaded into cur_time
      // or a time that is outside the range we want
      // no matter what if it's within the range we want, we should just print it.
      if(time_diff(range->time2, &cur_time) > 0)
	goto print_file_range_print_buffer;
    }

    // linear search for the end
    buf_ptr = start_ptr;
    while(1){
      reg_err = regexec(date_reg, buf_ptr, 2, date_match, 0);
      if(reg_err == REG_NOMATCH)
	break;
      else if(reg_err)
	printf("forever alone\n");
      else{
	sscanf(buf_ptr + date_match[1].rm_so, "%d%d:%d:%d", &cur_day, &(cur_time.hour), &(cur_time.min), &(cur_time.sec));
	cur_time.day = (cur_day == range->start_day) ? 0 : 1;

	if(time_diff(&cur_time, range->time2) > 0){ // if we're strictly after time2
	  buf_ptr[date_match[0].rm_so] = '\0'; // termitate this string
	  end_not_found = 0;
	  break;
	}
	else
	  buf_ptr += date_match[0].rm_eo;
      }
    }

    
  print_file_range_print_buffer:
    printf("%s",start_ptr);

    if(end_not_found){
      if(0 >= fread(buffer,1,BUFFER_SIZE,log_file)){
	fprintf(stderr, "hit eof");
	if(feof(log_file))
	  break;
	else // not sure if I should do something else on a diff err...
	  break;
      }
      buffer[BUFFER_SIZE] = '\0';
      start_ptr = buffer;
    }
  }
  return 0;
}/*}}}*/

/*  returns an estimate of the offset difference between tc1 and time1 
    - estimate assumes the logs are distributed ~regularly over the interval */
off_t estimate_distance(file_time_range *range, const my_time *time1)
{/*{{{*/
  // We're never going to do better than the second before time1, because if we're at time1
  // we might have gone too far.  Therefore, if we're at the second before, return 1
  // (we need to differentiate this from when we're at time1, which is a 0
  if(time_diff(time1, range->ct1) == 1) return 1;

  if(time_diff(range->ct2, range->ct1) == 0) return 0;

  off_t total_distance = range->off2 - range->off1;
  
  double distance_ratio = (double) time_diff(time1, range->ct1) / 
			  (double) time_diff(range->ct2, range->ct1);

  return (off_t) (total_distance * distance_ratio);
}/*}}}*/

/* set's up a new file_time_range for the next recursive call
   - takes start_time, time1, time2 from old_r
   - takes ct1 and ct2 from ct1 and ct2
   - takes off1 and off2 from off1 and off2

   ORDER OF ARGS: old_range, new_range, curtime1, curtime2, curoff1, curoff2

   returns a 0 always, until I realize some way this can screw up.  */
int set_range(file_time_range *old_r, file_time_range *new_r,
	      my_time *ct1, my_time *ct2,
	      off_t off1, off_t off2)
{/*{{{*/
  new_r->start_day	= old_r->start_day;
  new_r->start_time	= old_r->start_time;
  new_r->pre_time1	= old_r->pre_time1;
  new_r->avg_sec_size	= old_r->avg_sec_size;
  new_r->time1		= old_r->time1;
  new_r->time2		= old_r->time2;
  
  new_r->ct1 = ct1;
  new_r->ct2 = ct2;

  new_r->off1 = off1;
  new_r->off2 = off2;
}/*}}}*/


/* parses a time range from string input, storing the result in times
  
   returns 1 on success, 0 on a non-match			      */
int get_my_times(const char *input, my_time *time1, my_time *time2)
{/*{{{*/
  char		input_copy[50];
  char		*a;
  char		t1[50];		  // will store up to the -
  char		t2[50];		  // will store after the -
  int		exists_2nd = 0;   // will be set to 1 if there is stuff after -

  // strtok can damage the input string, might as well copy it
  if(strlen(input) > 49) return 0;
  strncpy(input_copy, input, 50);

  a = strtok(input_copy, "-");
  strncpy(t1, a, 50);

  a = strtok(NULL, "-");
  if(a != NULL){
    exists_2nd = 1;
    strncpy(t2, a, 50);
  }
  else
    strncpy(t2, t1, 50);

  // if we didn't get any data
  if(NULL == t1) return 0;

  a = strtok(t1, ":");
  if(0 > (time1->hour = limit_atoi(a, 24)))
    return 0;

  a = strtok(NULL, ":");
  if(NULL == a)
    time1->min = 0;
  else if(0 > (time1->min = limit_atoi(a, 60)))
    return 0;

  a = strtok(NULL, ":");
  if(NULL == a)
    time1->sec = 0;
  else if(0 > (time1->sec = limit_atoi(a, 60)))
    return 0;

  a = strtok(t2, ":");
  if(NULL == a)
      time2->hour = time1->hour;
  else if(0 > (time2->hour = limit_atoi(a, 24)))
    return 0;

  a = strtok(NULL, ":");
  if(NULL == a)
    time2->min = (exists_2nd) ? 0 : 59;
  else if(0 > (time2->min = limit_atoi(a, 60)))
    return 0;

  a = strtok(NULL, ":");
  if(NULL == a)
    time2->sec = (exists_2nd) ? 0 : 59;
  else if(0 > (time2->sec = limit_atoi(a, 60)))
    return 0;

  return 1;
}/*}}}*/

/* parses "a" to get a number >= 0 but < high
   note that "a" must be null terminated.
  
   returns -1 if fails, number on success     */
int limit_atoi(char *a, const int high)
{/*{{{*/
  // make sure it's all numeric chars
  for(char *c = a; *c != '\0'; c++)
    if(!('0' <= *c && *c <= '9'))
      return -1;

  int ret = atoi(a);

  if(ret < 0 || high <= ret)
    return -1;

  return ret;
}/*}}}*/

/* my_time -> int */
int timetoi(const my_time *time1)
{/*{{{*/
  return time1->sec + 60*time1->min + 60*60*time1->hour + 60*60*24*time1->day;
}/*}}}*/

/*  returns the integer # seconds of (end_time - start_time) % (seconds in a day) */
int time_diff(const my_time *end_time, const my_time *start_time)
{/*{{{*/
  return timetoi(end_time) - timetoi(start_time);
}/*}}}*/

/* decrements the orig_time by 1 second, stores in decd_time */
int dec_time(my_time *decd_time, const my_time *orig_time)
{/*{{{*/
  memcpy(decd_time, orig_time, sizeof(my_time));
  decd_time->sec -= 1;
  if(0 > decd_time->sec){
    decd_time->sec += 60;
    decd_time->min -= 1;
  }
  if(0 > decd_time->min){
    decd_time->min += 60;
    decd_time->hour -= 1;
  }
  if(0 > decd_time->hour){
    decd_time->hour += 24;
  }
  return 0;
}/*}}}*/

/* prints the my_time*/
void print_my_time(const my_time *time1)
{/*{{{*/
  fprintf(stderr,"%d %d:%d:%d\n",time1->day, time1->hour, time1->min, time1->sec);
}/*}}}*/
