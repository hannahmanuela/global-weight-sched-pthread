#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sched.h>

#define CPU_NUM 4

namespace {

bool setAffinityToCpu(int cpuIndex) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpuIndex, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        std::perror("sched_setaffinity");
        return false;
    }
    return true;
}

bool setNiceValue(int niceValue) {
    // niceValue in [-20, 19]; lower = higher priority (higher weight)
    if (setpriority(PRIO_PROCESS, 0, niceValue) != 0) {
        std::perror("setpriority");
        return false;
    }
    return true;
}

double timespecToSec(const timespec &ts) {
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

struct CpuAndWall {
    double cpuSec;
    double wallSec;
};

CpuAndWall sampleCpuAndWall() {
    timespec cpuTs{};
    timespec wallTs{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpuTs);
    clock_gettime(CLOCK_MONOTONIC, &wallTs);
    return {timespecToSec(cpuTs), timespecToSec(wallTs)};
}

volatile unsigned long long sink = 0; // prevents optimization

void runBusyAndReport(const std::string &label) {
    // Pin to CPU 0
    setAffinityToCpu(CPU_NUM);

    // Initial samples
    CpuAndWall prev = sampleCpuAndWall();

    // Busy work + once-per-second reporting
    while (true) {
        // Busy loop chunk
        for (int i = 0; i < 50'000; ++i) {
            sink += static_cast<unsigned long long>(i);
        }

        CpuAndWall now = sampleCpuAndWall();
        double dCpu = now.cpuSec - prev.cpuSec;
        double dWall = now.wallSec - prev.wallSec;

        if (dWall >= 1.0) {
            double pct = (dCpu / dWall) * 100.0;
            std::cout << "PID=" << getpid() << " [" << label << "] CPU="
                      << pct << "% over last ~" << dWall << "s\n";
            std::cout.flush();
            prev = now;
        } 
        // else {
            // Sleep a bit to aim for ~1s cadence, while still staying CPU-bound overall
            // struct timespec req { 0, 50 * 1000 * 1000 }; // 50ms
            // nanosleep(&req, nullptr);
        // }
    }
}

}

int main() {
    // Parent will create two children; both children will run indefinitely
    pid_t childLow = fork();
    if (childLow < 0) {
        std::perror("fork low");
        return 1;
    }

    if (childLow == 0) {
        // Low weight: nice=19
        if (!setNiceValue(19)) {
            std::cerr << "PID=" << getpid() << ": failed to set nice to 19 (lowest weight).\n";
        }
        // struct sched_param param;
        // param.sched_priority = 0;
        // if (sched_setscheduler(getpid(), SCHED_IDLE, &param) != 0) {
        //     std::perror("sched_setscheduler");
        // }

        runBusyAndReport("idle");
        return 0;
    }

    pid_t childHigh = fork();
    if (childHigh < 0) {
        std::perror("fork high");
        return 1;
    }

    if (childHigh == 0) {
        // High weight: nice=-20 (may require CAP_SYS_NICE)
        if (!setNiceValue(-20)) {
            std::cerr << "PID=" << getpid() << ": failed to set nice to -20 (highest weight)."
                      << " You may need privileges; continuing with current nice.\n";
        }
        runBusyAndReport("normal");
        return 0;
    }

    // Parent: ensure both children share the same CPU as well (optional)
    setAffinityToCpu(CPU_NUM);

    std::cout << "Spawned children PIDs: low=" << childLow << ", high=" << childHigh
              << ". Both pinned to CPU " << CPU_NUM << ". Press Ctrl+C to stop.\n";
    std::cout.flush();

    // Wait forever (children run until killed)
    // Reap children on exit
    int status = 0;
    while (true) {
        pid_t done = waitpid(-1, &status, 0);
        if (done == -1) {
            std::perror("waitpid");
            break;
        }
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            std::cout << "Child " << done << " exited.\n";
            std::cout.flush();
        }
    }

    return 0;
}


