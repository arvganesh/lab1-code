#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <sys/resource.h>
#include <unistd.h>
#include <string>
#include <sched.h>
#include <stats.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h> /* Definition of HW_* constants */
#include <sys/syscall.h>         /* Definition of SYS_* constants */

#define CACHE_LINE_SIZE 64

double timevalToDouble(const timeval& t) {
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_usec) / 1000000.0;
}

std::vector<double> get_resource_usage() {
    struct rusage usage;

    // Get resource usage statistics
    if (getrusage(RUSAGE_SELF, &usage) == -1) {
        perror("Error getting resource usage");
        return;
    }

    // Print the desired fields from struct rusage
    printf("utime: %ld.%06ld seconds\n", usage.ru_utime.tv_sec, usage.ru_utime.tv_usec);
    printf("stime: %ld.%06ld seconds\n", usage.ru_stime.tv_sec, usage.ru_stime.tv_usec);
    printf("maxrss (KB): %ld\n", usage.ru_maxrss);
    printf("minflt: %ld\n", usage.ru_minflt);
    printf("majflt: %ld\n", usage.ru_majflt);
    printf("inblock: %ld\n", usage.ru_inblock);
    printf("outblock: %ld\n", usage.ru_oublock);
    printf("nvcsw: %ld\n", usage.ru_nvcsw);
    printf("nivcsw: %ld\n", usage.ru_nivcsw);

   // Pack into results vector.
   std::vector<double> results = {
      timevalToDouble(usage.ru_utime),
      timevalToDouble(usage.ru_stime),
      (double) usage.ru_maxrss,
      (double) usage.ru_minflt,
      (double) usage.ru_majflt,
      (double) usage.ru_inblock,
      (double) usage.ru_oublock,
      (double) usage.ru_nvcsw,
      (double) usage.ru_nivcsw
   };

   return results;
}

long x = 1, y = 4, z = 7, w = 13;
long simplerand(void) {
	long t = x;
	t ^= t << 11;
	t ^= t >> 8;
	x = y;
	y = z;
	z = w;
	w ^= w >> 19;
	w ^= t;
	return w;
}

// p points to a region that is 1GB (ideally)
int opt_random_access = 0;
void do_mem_access(char* p, int size) {
   int i, j, count, outer, locality;
   int ws_base = 0;
   int max_base = ((size / CACHE_LINE_SIZE) - 512);
	for(outer = 0; outer < (1<<20); ++outer) {
      long r = simplerand() % max_base;
      // Pick a starting offset
      if (opt_random_access) {
         ws_base = r;
      } else {
         ws_base += 512;
         if( ws_base >= max_base ) {
            ws_base = 0;
         }
      }
      for(locality = 0; locality < 16; locality++) {
         volatile char *a;
         char c;
         for (i = 0; i < 512; i++) {
            // Working set of 512 cache lines, 32KB
            a = p + (ws_base + i) * CACHE_LINE_SIZE;
            if((i%8) == 0) {
               *a = 1;
            } else {
               c = *a;
            }
         }
      }
   }
}

void clearCache() {
   int total_cache_size = 48 * (1 << 10);
   char* buffer = (char*) malloc(total_cache_size);
   for (int i = 0; i < total_cache_size; i += 64) {
      char c = buffer[i];
      buffer[i] = 'x';
   }
}

#define SET_CONFIG(attr, cache_id, cache_op_id, cache_op_result_id) attr.config = (cache_id) | \
                               (cache_op_id << 8) | \
                               (cache_op_result_id << 16);

void startPerf() {
   int fd, retval;
   int pid = 0; // measure calling process
   int cpu = -1; // run on any cpu
   int flags = 0;

   struct perf_event_attr attr = {
      .type = PERF_TYPE_HW_CACHE,
      .size = sizeof(struct perf_event_attr),
      .config = 0
   };

   SET_CONFIG(attr, PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_ACCESS);
   fd = syscall(SYS_perf_event_open, &attr, pid, cpu, -1, flags);
   if (fd == -1) {
      perror("startPerf");
      exit(-1);
   }

   SET_CONFIG(attr, PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_ACCESS);
   retval = syscall(SYS_perf_event_open, &attr, pid, cpu, fd, flags);
   if (retval == -1) {
      perror("startPerf");
      exit(-1);
   }

   SET_CONFIG(attr, PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_ACCESS);
   retval = syscall(SYS_perf_event_open, &attr, pid, cpu, fd, flags);
   if (retval == -1) {
      perror("startPerf");
      exit(-1);
   }

   SET_CONFIG(attr, PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_ACCESS);
   fd = syscall(SYS_perf_event_open, &attr, pid, cpu, -1, flags);
   if (retval == -1) {
      perror("startPerf");
      exit(-1);
   }

   SET_CONFIG(attr, PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_ACCESS);
   retval = syscall(SYS_perf_event_open, &attr, pid, cpu, fd, flags);
   if (retval == -1) {
      perror("startPerf");
      exit(-1);
   }

}

int use_malloc = 1;
struct exp_results runExperiment() {
   clearCache();

   char* buffer;
   int buf_size = (1 << 30);
   if (use_malloc) {
      buffer = (char*) malloc(buf_size);
   } else {
      // use mmap.
   }

   // turn on perf.
   startPerf();

   do_mem_access(buffer, buf_size);

   // turn off perf.
   // stopPerf();

   std::vector<double> rusage_vec = get_resource_usage();
}  

void parseArgs(int argc, char* argv[]) {
   std::vector<std::string> args(argv + 1, argv + argc);

   for (auto it = args.begin(); it != args.end(); ++it) {
      if (*it == "random") {
         opt_random_access = 1;
      } else if (*it == "mmap") {
         use_malloc = 0;
      }
   }
}

void lockToSingleProcessor() {
   cpu_set_t set;
   CPU_ZERO(&set);
   CPU_SET(1, &set);
   if (sched_setaffinity(0, sizeof(set), &set) == -1) {
      perror("lockToSingleProcessor");
      exit(-1);
      return;
   } 
}

int main(int argc, char* argv[]) {
   parseArgs(argc, argv);
   lockToSingleProcessor();
   runTrials(5);
   return 0;
}