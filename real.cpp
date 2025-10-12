#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <system_error>
#include <string>
#include <thread>
#include <vector>

static constexpr int kNumCgroups = 10;
static constexpr int kThreadsPerGroup = 20;

static std::atomic<bool> stop_flag{false};

static void on_sigint(int sig) {
  (void)sig;
  stop_flag.store(true, std::memory_order_relaxed);
}

static int mkdir_p(const char *path, mode_t mode) {
  if (mkdir(path, mode) == 0) return 0;
  if (errno == EEXIST) return 0;
  return -1;
}

static int write_all(int fd, const char *buf, size_t len) {
  size_t off = 0;
  while (off < len) {
    ssize_t n = write(fd, buf + off, len - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    off += static_cast<size_t>(n);
  }
  return 0;
}

static int write_pid_to_controller(const char *file_path, pid_t pid) {
  int fd = open(file_path, O_WRONLY | O_CLOEXEC);
  if (fd < 0) return -1;
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%d", static_cast<int>(pid));
  int rc = write_all(fd, buf, static_cast<size_t>(len));
  int saved = errno;
  close(fd);
  errno = saved;
  return rc;
}

static int move_pid_to_cgroup(const char *cg_path, pid_t pid) {
  char procs_path[512];
  snprintf(procs_path, sizeof(procs_path), "%s/cgroup.procs", cg_path);
  if (write_pid_to_controller(procs_path, pid) == 0) return 0;
  snprintf(procs_path, sizeof(procs_path), "%s/tasks", cg_path);
  if (write_pid_to_controller(procs_path, pid) == 0) return 0;
  return -1;
}

static void busy_thread() {
  volatile uint64_t sink = 0;
  while (!stop_flag.load(std::memory_order_relaxed)) {
    sink = sink * 1664525u + 1013904223u;
    sink ^= sink >> 13;
    sink += 0x9e3779b97f4a7c15ull;
  }
  asm volatile("" :: "r"(sink));
}

static void run_worker_group() {
  std::vector<std::thread> threads;
  threads.reserve(kThreadsPerGroup);
  for (int i = 0; i < kThreadsPerGroup; ++i) {
    try {
      threads.emplace_back(busy_thread);
    } catch (const std::system_error &e) {
      fprintf(stderr, "thread create failed: %s\n", e.what());
      stop_flag.store(true, std::memory_order_relaxed);
      break;
    }
  }
  for (auto &t : threads) {
    if (t.joinable()) t.join();
  }
}

int main() {
  const char *mount_point = "/sys/fs/cgroup"; // cgroup v2 default

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_sigint;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);

  pid_t children[kNumCgroups] = {0};
  char cgroup_paths[kNumCgroups][512];

  for (int i = 0; i < kNumCgroups; ++i) {
    snprintf(cgroup_paths[i], sizeof(cgroup_paths[i]), "%s/realgrp_%02d", mount_point, i);
    if (mkdir_p(cgroup_paths[i], 0755) != 0) {
      perror("mkdir cgroup");
      exit(1);
    }

    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      stop_flag.store(true, std::memory_order_relaxed);
      break;
    }
    if (pid == 0) {
      // if (move_pid_to_cgroup(cgroup_paths[i], getpid()) != 0) {
      //   perror("write cgroup.procs/tasks");
      //   _exit(2);
      // }
      run_worker_group();
      _exit(0);
    } else {
      children[i] = pid;
    }
  }

  while (!stop_flag.load(std::memory_order_relaxed)) {
    pause();
  }

  for (int i = 0; i < kNumCgroups; ++i) {
    if (children[i] > 0) kill(children[i], SIGINT);
  }
  for (int i = 0; i < kNumCgroups; ++i) {
    if (children[i] > 0) {
      int status;
      waitpid(children[i], &status, 0);
    }
  }

  for (int i = 0; i < kNumCgroups; ++i) {
    if (cgroup_paths[i][0]) {
      if (rmdir(cgroup_paths[i]) != 0) {
        fprintf(stderr, "Warning: failed to remove %s: %s\n", cgroup_paths[i], strerror(errno));
      }
    }
  }

  return 0;
}


