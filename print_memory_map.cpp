#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>
#include <stats.h>

struct rusage print_resource_usage() {
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

    return rusage;
}

int main() {
    FILE *maps_file;
    char buffer[1000];

    maps_file = fopen("/proc/self/maps", "r");
    if (maps_file == NULL) {
        perror("Error opening /proc/self/maps");
        return 1;
    }

    while (fgets(buffer, sizeof(buffer), maps_file) != NULL) {
        printf("%s", buffer);
    }

    fclose(maps_file);

    return 0;
}