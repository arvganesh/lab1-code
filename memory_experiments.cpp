#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <sys/resource.h>
#include <unistd.h>
#include <string>
#include <string.h>
#include <assert.h>
#include <sched.h>
#include <fcntl.h>
#include <map>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h> /* Definition of HW_* constants */
#include <sys/syscall.h>         /* Definition of SYS_* constants */
#include <sys/ioctl.h>
#include "stats.h"
#include <sys/mman.h>

#define CACHE_LINE_SIZE 64
#define CHECK_ERR(call, name) \
   if ((call) == -1) { \
      perror(name); \
      exit(-1); \
   }

struct read_format {
  uint64_t nr;
  struct {
    uint64_t value;
    uint64_t id;
  } values[];
};

typedef std::map<std::string, double> exp_results;
typedef std::map<std::string, Stats> stat_storage;

double timevalToDouble(const timeval& t) {
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_usec) / 1000000.0;
}

int debug = 0;
void get_resource_usage(exp_results& results) {
    struct rusage usage;

    // Get resource usage statistics
    if (getrusage(RUSAGE_SELF, &usage) == -1) {
        perror("Error getting resource usage");
        return;
    }

    // Print the desired fields from struct rusage
    if (debug) {
      printf("utime: %ld.%06ld seconds\n", usage.ru_utime.tv_sec, usage.ru_utime.tv_usec);
      printf("stime: %ld.%06ld seconds\n", usage.ru_stime.tv_sec, usage.ru_stime.tv_usec);
      printf("maxrss (KB): %ld\n", usage.ru_maxrss);
      printf("minflt: %ld\n", usage.ru_minflt);
      printf("majflt: %ld\n", usage.ru_majflt);
      printf("inblock: %ld\n", usage.ru_inblock);
      printf("outblock: %ld\n", usage.ru_oublock);
      printf("nvcsw: %ld\n", usage.ru_nvcsw);
      printf("nivcsw: %ld\n", usage.ru_nivcsw);
    }

   // Store results.
   results["user_time"] = timevalToDouble(usage.ru_utime);
   results["system_time"] = timevalToDouble(usage.ru_stime);
   results["max_resident_set_size"] = static_cast<double>(usage.ru_maxrss);
   results["page_faults_minor"] = static_cast<double>(usage.ru_minflt);
   results["page_faults_major"] = static_cast<double>(usage.ru_majflt);
   results["block_input_ops"] = static_cast<double>(usage.ru_inblock);
   results["block_output_ops"] = static_cast<double>(usage.ru_oublock);
   results["voluntary_context_switches"] = static_cast<double>(usage.ru_nvcsw);
   results["involuntary_context_switches"] = static_cast<double>(usage.ru_nivcsw);
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

typedef void (*mem_access_function)(char*, int);

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

#define SET_CONFIG(attr, cache_id, cache_op_id, cache_op_result_id) attr->config = (cache_id) | \
                               (cache_op_id << 8) | \
                               (cache_op_result_id << 16);
int setup_L1D(struct perf_event_attr* attr, int* l1d_read_access_id, int* l1d_read_miss_id, int* l1d_write_access_id) {
   int fd, retval;
   int pid = 0; // measure calling process
   int cpu = -1; // run on any cpu
   int flags = 0;

   // First event group.
   SET_CONFIG(attr, PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_ACCESS);
   attr->disabled = 1;
   fd = syscall(SYS_perf_event_open, attr, pid, cpu, -1, flags);
   if (fd == -1) {
      perror("startPerf");
      exit(-1);
   }
   CHECK_ERR(ioctl(fd, PERF_EVENT_IOC_ID, l1d_read_access_id), "ioctl");

   SET_CONFIG(attr, PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS);
   attr->disabled = 0;
   retval = syscall(SYS_perf_event_open, attr, pid, cpu, fd, flags);
   if (retval == -1) {
      perror("startPerf");
      exit(-1);
   }
   CHECK_ERR(ioctl(retval, PERF_EVENT_IOC_ID, l1d_read_miss_id), "ioctl");

   SET_CONFIG(attr, PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_WRITE, PERF_COUNT_HW_CACHE_RESULT_ACCESS);
   attr->disabled = 0;
   retval = syscall(SYS_perf_event_open, attr, pid, cpu, fd, flags);
   if (retval == -1) {
      perror("startPerf");
      exit(-1);
   }
   CHECK_ERR(ioctl(retval, PERF_EVENT_IOC_ID, l1d_write_access_id), "ioctl");

   return fd;
}

int setup_TLB(struct perf_event_attr* attr, int* dtlb_read_miss_id, int* dtlb_write_miss_id) {
   int fd, retval;
   int pid = 0; // measure calling process
   int cpu = -1; // run on any cpu
   int flags = 0;

   // Second event group.
   SET_CONFIG(attr, PERF_COUNT_HW_CACHE_DTLB, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS);
   attr->disabled = 1;
   assert(attr->config != 0);
   fd = syscall(SYS_perf_event_open, attr, pid, cpu, -1, flags);
   if (retval == -1) {
      perror("startPerf");
      exit(-1);
   }
   CHECK_ERR(ioctl(fd, PERF_EVENT_IOC_ID, dtlb_read_miss_id), "ioctl");

   SET_CONFIG(attr, PERF_COUNT_HW_CACHE_DTLB, PERF_COUNT_HW_CACHE_OP_WRITE, PERF_COUNT_HW_CACHE_RESULT_MISS);
   attr->disabled = 0;
   retval = syscall(SYS_perf_event_open, attr, pid, cpu, fd, flags);
   if (retval == -1) {
      perror("startPerf");
      exit(-1);
   }
   CHECK_ERR(ioctl(retval, PERF_EVENT_IOC_ID, dtlb_write_miss_id), "ioctl");

   std::cout << "tlb read miss id: " << *dtlb_read_miss_id << " tlb write miss id: " << *dtlb_write_miss_id << std::endl;

   return fd;
}

void recordL1D(exp_results& results, int fd, int read_access_id, int read_miss_id, int write_access_id) {
   char buffer[4096];
   CHECK_ERR(read(fd, buffer, sizeof(buffer)), "read");
   struct read_format* formatted_bytes = (struct read_format*) buffer;
   for (int i = 0; i < formatted_bytes->nr; ++i) {
      auto cur = formatted_bytes->values[i];
      if (cur.id == read_access_id) {
         results["l1d_read_access"] = cur.value;
      } else if (cur.id == read_miss_id) {
         results["l1d_read_miss"] = cur.value;
      } else if (cur.id == write_access_id) {
         results["l1d_write_access"] = cur.value;
      }
   }
}

void recordTLB(exp_results& results, int fd, int read_miss_id, int write_miss_id) {
   char buffer[4096];
   CHECK_ERR(read(fd, buffer, sizeof(buffer)), "read");
   struct read_format* formatted_bytes = (struct read_format*) buffer;
   for (int i = 0; i < formatted_bytes->nr; ++i) {
      auto cur = formatted_bytes->values[i];
      if (cur.id == read_miss_id) {
         results["tlb_read_miss"] = cur.value;
      } else if (cur.id == write_miss_id) {
         results["tlb_write_miss"] = cur.value;
      }
   }
}

exp_results runWithPerf(mem_access_function function_to_measure, char* buffer, int size) {
   // Event IDs.
   int L1D_read_access_id, L1D_read_miss_id, L1D_write_access_id; 
   int TLB_read_miss_id, TLB_write_miss_id; 

   struct perf_event_attr attr;
   memset(&attr, 0, sizeof(attr));
   attr.type = PERF_TYPE_HW_CACHE;
   attr.size = sizeof(attr);
   attr.exclude_kernel = 1;
   attr.exclude_hv = 1;
   attr.disabled = 1;
   attr.read_format = (PERF_FORMAT_GROUP | PERF_FORMAT_ID);

   // Set up perf FDs, they are initially disabled.
   // Read Event IDs.
   int TLB_fd = setup_TLB(&attr, &TLB_read_miss_id, &TLB_write_miss_id);
   int L1D_fd = setup_L1D(&attr, &L1D_read_access_id, &L1D_read_miss_id, &L1D_write_access_id);

   // Start recording.
   CHECK_ERR(ioctl(TLB_fd, PERF_EVENT_IOC_RESET, 0), "ioctl");
   CHECK_ERR(ioctl(L1D_fd, PERF_EVENT_IOC_RESET, 0), "ioctl");
   CHECK_ERR(ioctl(TLB_fd, PERF_EVENT_IOC_ENABLE, 0), "ioctl");
   CHECK_ERR(ioctl(L1D_fd, PERF_EVENT_IOC_ENABLE, 0), "ioctl");

   // Run function to measure.
   function_to_measure(buffer, size);

   // Stop recording.
   CHECK_ERR(ioctl(L1D_fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP), "ioctl");
   CHECK_ERR(ioctl(TLB_fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP), "ioctl");

   // Read results.
   exp_results results;
   std::cout << "tlb read miss id: " << TLB_read_miss_id << " tlb write miss id: " << TLB_write_miss_id << std::endl;
   recordL1D(results, L1D_fd, L1D_read_access_id, L1D_read_miss_id, L1D_write_access_id);
   recordTLB(results, TLB_fd, TLB_read_miss_id, TLB_write_miss_id);

   // Record resource usage.
   get_resource_usage(results);

   return results;
}

int use_malloc = 1;
int use_map_shared = 0;
int use_map_populate = 0;
int use_file_backing = 0;
int use_msync = 0;
char* allocateBuffer(int buf_size) {
   char* buffer;
   if (use_malloc) {
      buffer = (char*) malloc(buf_size);
   } else {
      // use mmap.
      int prot = PROT_READ | PROT_WRITE;
      int flags = (use_map_shared ? MAP_SHARED : MAP_PRIVATE) | (use_map_populate ? MAP_POPULATE : 0);
      int fd = -1;
      int offset = 0;

      if (use_file_backing) {
         fd = open("sample.txt", O_RDWR, "rw"); // open 1 GB file.
         CHECK_ERR(fd, "open");
      } else {
         flags |= MAP_ANONYMOUS;
      }
      buffer = (char*) mmap(NULL, buf_size, prot, flags, fd, offset);
   }

   CHECK_ERR((buffer == NULL) ? -1 : 0, "1 GB buffer allocation failed.");

   if (use_msync && use_file_backing) {
      memset(buffer, 0xFF, buf_size);
      CHECK_ERR(msync(buffer, buf_size, MS_ASYNC), "msync");
   }

   return buffer;
}

exp_results runExperiment() {
   clearCache();
   int buf_size = 1 << 30;
   char* buffer = allocateBuffer(buf_size);
   exp_results results = runWithPerf(do_mem_access, buffer, buf_size);
   return results;
}

void recordTrial(stat_storage& stats, exp_results& trial, bool initialize) {
   for (const auto& [metric, value] : trial) {
      if (initialize) {
         stats[metric] = Stats(metric);
      }
      stats[metric].add(value);
   }
}

void summarize(stat_storage stats) {
   for (const auto& metric_to_stats : stats) {
      const auto& metric_name = metric_to_stats.first;
      const auto& data = metric_to_stats.second;
      std::cout << data << std::endl;
   }
}

void runTrials(int numTrials) {
   stat_storage stats;
   for (int i = 0; i < numTrials; ++i) {
      std::cout << "Running Trial " << i << std::endl;
      exp_results trial = runExperiment();
      recordTrial(stats, trial, i == 0);
   }
   summarize(stats);
}


void parseArgs(int argc, char* argv[]) {
   std::vector<std::string> args(argv + 1, argv + argc);

   for (auto it = args.begin(); it != args.end(); ++it) {
      if (*it == "random") {
         opt_random_access = 1;
      } else if (*it == "mmap") {
         use_malloc = 0;
      } else if (*it == "shared") {
         use_map_shared = 1;
      } else if (*it == "prefault") {
         use_map_populate = 1;
      } else if (*it == "filebacked") {
         use_file_backing = 1;
      } else if (*it == "msync") {
         use_msync = 1;
      } else if (*it == "debug") {
         debug = 1;
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
   runTrials(1);
   return 0;
}