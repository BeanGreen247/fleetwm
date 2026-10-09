// Bulk transfer over a Unix stream socket and over a pipe with different buffer sizes (PERFORMANCE_FINDINGS 2.5).
// Usage: ipcbuf [MB] [rounds]. Prints best-of-rounds GB/s and CPU seconds (both processes) per variant.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static double cpu_children(void) { struct rusage r; getrusage(RUSAGE_CHILDREN, &r); return r.ru_utime.tv_sec + r.ru_utime.tv_usec * 1e-6 + r.ru_stime.tv_sec + r.ru_stime.tv_usec * 1e-6; }

// kind 0 = socketpair stream, 1 = pipe. bufsz 0 = kernel default. chunk = write and read size.
static int run(int kind, int bufsz, size_t chunk, size_t total, double *secs, double *cpu) {
  int fds[2];
  if (kind == 0) { if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds)) return -1; }
  else { if (pipe(fds)) return -1; int t = fds[0]; fds[0] = fds[1]; fds[1] = t; }
  if (bufsz) {
    if (kind == 0) {
      setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &bufsz, sizeof bufsz);
      setsockopt(fds[1], SOL_SOCKET, SO_RCVBUF, &bufsz, sizeof bufsz);
    } else if (fcntl(fds[1], F_SETPIPE_SZ, bufsz) < 0) { return -2; }
  }
  double c0 = cpu_children();
  double t0 = now();
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    char *buf = malloc(chunk);
    size_t got = 0;
    while (got < total) { ssize_t n = read(fds[1], buf, chunk); if (n <= 0) break; got += n; }
    _exit(got == total ? 0 : 1);
  }
  close(fds[1]);
  char *buf = malloc(chunk);
  memset(buf, 'x', chunk);
  size_t sent = 0;
  while (sent < total) { size_t w = total - sent < chunk ? total - sent : chunk; ssize_t n = write(fds[0], buf, w); if (n <= 0) break; sent += n; }
  close(fds[0]);
  int st; waitpid(pid, &st, 0);
  *secs = now() - t0;
  *cpu = cpu_children() - c0;  // reader only; writer CPU added below
  free(buf);
  return st == 0 ? 0 : -3;
}

int main(int argc, char **argv) {
  size_t total = (argc > 1 ? atoi(argv[1]) : 256) * 1024UL * 1024UL;
  int rounds = argc > 2 ? atoi(argv[2]) : 7;
  struct { int kind; int buf; size_t chunk; } v[] = {
    {0, 0, 4096}, {0, 0, 65536}, {0, 262144, 65536}, {0, 1 << 20, 65536}, {0, 4 << 20, 65536}, {0, 4 << 20, 1 << 20},
    {1, 0, 4096}, {1, 0, 65536}, {1, 1 << 20, 65536}, {1, 4 << 20, 65536}, {1, 4 << 20, 1 << 20},
  };
  int n = sizeof v / sizeof v[0];
  double best[32]; for (int i = 0; i < n; i++) best[i] = 1e9;
  double bestcpu[32] = {0};
  for (int r = 0; r < rounds; r++)
    for (int i = 0; i < n; i++) {
      double s, c;
      struct rusage a, b; getrusage(RUSAGE_SELF, &a);
      int rc = run(v[i].kind, v[i].buf, v[i].chunk, total, &s, &c);
      getrusage(RUSAGE_SELF, &b);
      double w = (b.ru_utime.tv_sec - a.ru_utime.tv_sec) + (b.ru_utime.tv_usec - a.ru_utime.tv_usec) * 1e-6 + (b.ru_stime.tv_sec - a.ru_stime.tv_sec) + (b.ru_stime.tv_usec - a.ru_stime.tv_usec) * 1e-6;
      if (rc) { best[i] = -1; continue; }
      if (s < best[i]) { best[i] = s; bestcpu[i] = c + w; }
    }
  printf("%zu MB, best of %d, interleaved\n%-8s %-9s %-8s %8s %8s\n", total >> 20, rounds, "kind", "buffer", "chunk", "GB/s", "cpu s");
  for (int i = 0; i < n; i++)
    printf("%-8s %-9d %-8zu %8.2f %8.2f\n", v[i].kind ? "pipe" : "unix", v[i].buf, v[i].chunk, best[i] > 0 ? total / best[i] / 1e9 : 0.0, bestcpu[i]);
  return 0;
}
