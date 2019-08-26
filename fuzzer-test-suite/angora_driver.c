//===- angora_driver.c - a glue between Angora and libFuzzer --*- C++ -* --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//===----------------------------------------------------------------------===//

/* This file allows to fuzz libFuzzer-style target functions
 (LLVMFuzzerTestOneInput) with Angora using a persistent (in-process) mode.

Usage:
################################################################################
cat << EOF > test_fuzzer.cc
#include <stddef.h>
#include <stdint.h>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size > 0 && data[0] == 'H')
    if (size > 1 && data[1] == 'I')
       if (size > 2 && data[2] == '!')
       __builtin_trap();
  return 0;
}
EOF
# Build your target with using fresh angora-clang.
angora-clang test_fuzzer.cc -c
# Build this file, link it with Angora runtime and the target code.
clang++ angora_driver.c test_fuzzer.o $ANGORA_HOME/bin/lib/libruntime_fast.a
# Run Angora:
rm -rf IN OUT; mkdir IN OUT; echo z > IN/z;
$ANGORA_HOME/bin/angora-fuzzer -i IN -o OUT ./a.out
################################################################################
Environment Variables:
There are a few environment variables that can be set to use features that
Angora doesn't have.

ANGORA_DRIVER_STDERR_DUPLICATE_FILENAME: Setting this *appends* stderr to the
file specified. If the file does not exist, it is created. This is useful for
getting stack traces (when using ASAN for example) or original error messages
on hard to reproduce bugs.

ANGORA_DRIVER_EXTRA_STATS_FILENAME: Setting this causes Angora to write extra
statistics to the file specified. Currently these are peak_rss_mb
(the peak amount of virtual memory used in MB) and slowest_unit_time_secs. If
the file does not exist it is created. If the file does exist then
angora_driver assumes it was restarted by Angora and will try to read old
statistics from the file. If that fails then the process will quit.

*/
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

// Platform detection. Copied from FuzzerInternal.h
#ifdef __linux__
#define LIBFUZZER_LINUX 1
#define LIBFUZZER_APPLE 0
#define LIBFUZZER_NETBSD 0
#define LIBFUZZER_FREEBSD 0
#define LIBFUZZER_OPENBSD 0
#elif __APPLE__
#define LIBFUZZER_LINUX 0
#define LIBFUZZER_APPLE 1
#define LIBFUZZER_NETBSD 0
#define LIBFUZZER_FREEBSD 0
#define LIBFUZZER_OPENBSD 0
#elif __NetBSD__
#define LIBFUZZER_LINUX 0
#define LIBFUZZER_APPLE 0
#define LIBFUZZER_NETBSD 1
#define LIBFUZZER_FREEBSD 0
#define LIBFUZZER_OPENBSD 0
#elif __FreeBSD__
#define LIBFUZZER_LINUX 0
#define LIBFUZZER_APPLE 0
#define LIBFUZZER_NETBSD 0
#define LIBFUZZER_FREEBSD 1
#define LIBFUZZER_OPENBSD 0
#elif __OpenBSD__
#define LIBFUZZER_LINUX 0
#define LIBFUZZER_APPLE 0
#define LIBFUZZER_NETBSD 0
#define LIBFUZZER_FREEBSD 0
#define LIBFUZZER_OPENBSD 1
#else
#error "Support for your platform has not been implemented"
#endif

// Used to avoid repeating error checking boilerplate. If cond is false, a
// fatal error has occurred in the program. In this event print error_message
// to stderr and abort(). Otherwise do nothing. Note that setting
// ANGORA_DRIVER_STDERR_DUPLICATE_FILENAME may cause error_message to be
// appended to the file as well, if the error occurs after the duplication is
// performed.
#define CHECK_ERROR(cond, error_message)                                       \
  if (!(cond)) {                                                               \
    fprintf(stderr, "%s\n", (error_message));                                  \
    abort();                                                                   \
  }

// libFuzzer interface is thin, so we don't include any libFuzzer headers.
int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size);
__attribute__((weak)) int LLVMFuzzerInitialize(int *argc, char ***argv);

#ifdef USE_FAST
extern void __angora_reset_context();

// From common/src/config.rs
#define MAP_SIZE_POW2   20
#define BRANCHES_SIZE   (1 << MAP_SIZE_POW2)
#endif

// Emulate an AFL-style persistent mode in Angora.
static int __angora_persistent_loop(unsigned int max_cnt) {
  static uint8_t first_pass = 1;
  static uint32_t cycle_cnt;

  if (first_pass) {
#ifdef USE_FAST
    __angora_reset_context();
#endif

    cycle_cnt = max_cnt;
    first_pass = 0;
    return 1;
  }

  if (--cycle_cnt) {
    raise(SIGSTOP);
#ifdef USE_FAST
    __angora_reset_context();
#endif

    return 1;
  }

  return 0;
}

// Emulate a deferred forkserver in Angora.
static const char *kForksrvSocketFile = "/tmp/forksrv_socket";
static int forksrv_sock;

static void  __angora_manual_init(void) {
#ifdef USE_FAST
  int rc, listener_sock;
  struct sockaddr_un listener_addr;

  listener_sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listener_sock == -1) {
    fprintf(stderr, "libFuzzer: socket failed with %d\n", errno);
    exit(1);
  }

  // Don't block
  fcntl(listener_sock, F_SETFL, O_NONBLOCK);

  listener_addr.sun_family = AF_UNIX;
  strcpy(listener_addr.sun_path, kForksrvSocketFile);

  rc = bind(listener_sock, (struct sockaddr *)&listener_addr, sizeof(listener_addr));
  if (rc == -1) {
    fprintf(stderr, "libFuzzer: bind failed with %d\n", errno);
    close(listener_sock);
    exit(1);
  }

  rc = listen(listener_sock, /* backlog */ 128);
  if (rc == -1) {
    fprintf(stderr, "libFuzzer: listen failed with %d\n", errno);
    close(listener_sock);
    exit(1);
  }

  setenv("ANGORA_ENABLE_FORKSRV", "TRUE", 1);
  setenv("ANGORA_FORKSRV_SOCKET_PATH", kForksrvSocketFile, 1);

  forksrv_sock = accept(listener_sock, NULL, NULL);
  if (forksrv_sock == -1) {
    fprintf(stderr, "libFuzzer: accept failed with %d\n", errno);
    close(listener_sock);
    close(forksrv_sock);
    exit(1);
  }

  // TODO set timeouts
#endif
}

// Input buffer.
static const size_t kMaxAngoraInputSize = 1 << 20;
static uint8_t AngoraInputBuf[kMaxAngoraInputSize];

// Variables we need for writing to the extra stats file.
static FILE *extra_stats_file = NULL;
static uint32_t previous_peak_rss = 0;
static time_t slowest_unit_time_secs = 0;
static const int kNumExtraStats = 2;
static const char *kExtraStatsFormatString = "peak_rss_mb            : %u\n"
                                             "slowest_unit_time_sec  : %u\n";

// Experimental feature to use angora_driver without ANGORA's deferred mode.
// Needs to run before __angora_auto_init.
__attribute__((constructor(0))) void __decide_deferred_forkserver(void) {
  if (getenv("ANGORA_DRIVER_DONT_DEFER")) {
    if (unsetenv("__ANGORA_DEFER_FORKSRV")) {
      perror("Failed to unset __ANGORA_DEFER_FORKSRV");
      abort();
    }
  }
}

// Copied from FuzzerUtil.cpp.
size_t GetPeakRSSMb() {
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage))
    return 0;
  if (LIBFUZZER_LINUX || LIBFUZZER_NETBSD || LIBFUZZER_FREEBSD ||
      LIBFUZZER_OPENBSD) {
    // ru_maxrss is in KiB
    return usage.ru_maxrss >> 10;
  } else if (LIBFUZZER_APPLE) {
    // ru_maxrss is in bytes
    return usage.ru_maxrss >> 20;
  }
  assert(0 && "GetPeakRSSMb() is not implemented for your platform");
  return 0;
}

// Based on SetSigaction in FuzzerUtil.cpp
static void SetSigaction(int signum,
                         void (*callback)(int, siginfo_t *, void *)) {
  struct sigaction sigact;
  memset(&sigact, 0, sizeof(sigact));
  sigact.sa_sigaction = callback;
  if (sigaction(signum, &sigact, 0)) {
    fprintf(stderr, "libFuzzer: sigaction failed with %d\n", errno);
    exit(1);
  }
}

// Write extra stats to the file specified by the user. If none is specified
// this function will never be called.
static void write_extra_stats() {
  uint32_t peak_rss = GetPeakRSSMb();

  if (peak_rss < previous_peak_rss)
    peak_rss = previous_peak_rss;

  int chars_printed = fprintf(extra_stats_file, kExtraStatsFormatString,
                              peak_rss, slowest_unit_time_secs);

  CHECK_ERROR(chars_printed != 0, "Failed to write extra_stats_file");

  CHECK_ERROR(fclose(extra_stats_file) == 0,
              "Failed to close extra_stats_file");
}

// Call write_extra_stats before we exit.
static void crash_handler(int sig, siginfo_t *info, void *ucontetxt) {
  // Make sure we don't try calling write_extra_stats again if we crashed while
  // trying to call it.
  static uint8_t first_crash = 1;
  CHECK_ERROR(first_crash,
              "Crashed in crash signal handler. This is a bug in the fuzzer.");

  first_crash = 0;
  write_extra_stats();
}

// If the user has specified an extra_stats_file through the environment
// variable ANGORA_DRIVER_EXTRA_STATS_FILENAME, then perform necessary set up
// to write stats to it on exit. If no file is specified, do nothing. Otherwise
// install signal and exit handlers to write to the file when the process exits.
// Then if the file doesn't exist create it and set extra stats to 0. But if it
// does exist then read the initial values of the extra stats from the file
// and check that the file is writable.
static void maybe_initialize_extra_stats() {
  // If ANGORA_DRIVER_EXTRA_STATS_FILENAME isn't set then we have nothing to do.
  char *extra_stats_filename = getenv("ANGORA_DRIVER_EXTRA_STATS_FILENAME");
  if (!extra_stats_filename)
    return;

  // Open the file and find the previous peak_rss_mb value.
  // This is necessary because the fuzzing process is restarted after N
  // iterations are completed. So we may need to get this value from a previous
  // process to be accurate.
  extra_stats_file = fopen(extra_stats_filename, "r");

  // If extra_stats_file already exists: read old stats from it.
  if (extra_stats_file) {
    int matches = fscanf(extra_stats_file, kExtraStatsFormatString,
                         &previous_peak_rss, &slowest_unit_time_secs);

    // Make sure we have read a real extra stats file and that we have used it
    // to set slowest_unit_time_secs and previous_peak_rss.
    CHECK_ERROR(matches == kNumExtraStats, "Extra stats file is corrupt");

    CHECK_ERROR(fclose(extra_stats_file) == 0, "Failed to close file");

    // Now open the file for writing.
    extra_stats_file = fopen(extra_stats_filename, "w");
    CHECK_ERROR(extra_stats_file,
                "Failed to open extra stats file for writing");
  } else {
    // Looks like this is the first time in a fuzzing job this is being called.
    extra_stats_file = fopen(extra_stats_filename, "w+");
    CHECK_ERROR(extra_stats_file, "failed to create extra stats file");
  }

  // Make sure that crash_handler gets called on any kind of fatal error.
  int crash_signals[] = {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE,  SIGINT,
                         SIGTERM};

  const size_t num_signals = sizeof(crash_signals) / sizeof(crash_signals[0]);

  for (size_t idx = 0; idx < num_signals; idx++)
    SetSigaction(crash_signals[idx], crash_handler);

  // Make sure it gets called on other kinds of exits.
  atexit(write_extra_stats);
}

// If the user asks us to duplicate stderr, then do it.
static void maybe_duplicate_stderr() {
  char* stderr_duplicate_filename =
      getenv("ANGORA_DRIVER_STDERR_DUPLICATE_FILENAME");

  if (!stderr_duplicate_filename)
    return;

  FILE* stderr_duplicate_stream =
      freopen(stderr_duplicate_filename, "a+", stderr);

  if (!stderr_duplicate_stream) {
    fprintf(
        stderr,
        "Failed to duplicate stderr to ANGORA_DRIVER_STDERR_DUPLICATE_FILENAME");
    abort();
  }
}

// Define LLVMFuzzerMutate to avoid link failures for targets that use it
// with libFuzzer's LLVMFuzzerCustomMutator.
size_t LLVMFuzzerMutate(uint8_t *Data, size_t Size, size_t MaxSize) {
  assert(0 && "LLVMFuzzerMutate should not be called from angora_driver");
  return 0;
}

// Execute any files provided as parameters.
int ExecuteFilesOnyByOne(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    FILE *f = fopen(argv[i], "r");
    assert(f);
    fseek(f, 0, SEEK_END);
    size_t length = ftell(f);
    fseek(f, 0, SEEK_SET);
    printf("Reading %lu bytes from %s\n", length, argv[i]);
    // Allocate exactly length bytes so that we reliably catch buffer overflows
    uint8_t *bytes = (uint8_t*)malloc(length);
    size_t n_read = fread(bytes, 1, length, f);
    assert(n_read == length);
    LLVMFuzzerTestOneInput(bytes, length);
    free(bytes);
    printf("Execution successful\n");
  }
  return 0;
}

int main(int argc, char **argv) {
  fprintf(stderr,
      "======================= INFO =========================\n"
      "This binary is built for Angora.\n"
      "To run the target function on individual input(s) execute this:\n"
      "  %s < INPUT_FILE\n"
      "or\n"
      "  %s INPUT_FILE1 [INPUT_FILE2 ... ]\n"
      "To fuzz with Angora execute this:\n"
      "  angora-fuzzer [angora-flags] %s [-N]\n"
      "angora-fuzzer will run N iterations before "
      "re-spawning the process (default: 1000)\n"
      "======================================================\n",
          argv[0], argv[0], argv[0]);
  if (LLVMFuzzerInitialize)
    LLVMFuzzerInitialize(&argc, &argv);
  // Do any other expensive one-time initialization here.

  maybe_duplicate_stderr();
  maybe_initialize_extra_stats();

  if (!getenv("ANGORA_DRIVER_DONT_DEFER"))
    __angora_manual_init();

  int N = 1000;
  if (argc == 2 && argv[1][0] == '-')
      N = atoi(argv[1] + 1);
  else if(argc == 2 && (N = atoi(argv[1])) > 0)
      fprintf(stderr, "WARNING: using the deprecated call style `%s %d`\n",
              argv[0], N);
  else if (argc > 1)
    return ExecuteFilesOnyByOne(argc, argv);

  assert(N > 0);

  // Call LLVMFuzzerTestOneInput here so that coverage caused by initialization
  // on the first execution of LLVMFuzzerTestOneInput is ignored.
  uint8_t dummy_input[1] = {0};
  LLVMFuzzerTestOneInput(dummy_input, 1);

  time_t unit_time_secs;
  int num_runs = 0;
  while (__angora_persistent_loop(N)) {
    ssize_t n_read = read(0, AngoraInputBuf, kMaxAngoraInputSize);
    if (n_read > 0) {
      // Copy AngoraInputBuf into a separate buffer to let asan find buffer
      // overflows. Don't use unique_ptr/etc to avoid extra dependencies.
      uint8_t *copy = (uint8_t*)malloc(n_read);
      memcpy(copy, AngoraInputBuf, n_read);

      struct timeval unit_start_time;
      CHECK_ERROR(gettimeofday(&unit_start_time, NULL) == 0,
                  "Calling gettimeofday failed");

      num_runs++;
      LLVMFuzzerTestOneInput(copy, n_read);

      struct timeval unit_stop_time;
      CHECK_ERROR(gettimeofday(&unit_stop_time, NULL) == 0,
                  "Calling gettimeofday failed");

      // Update slowest_unit_time_secs if we see a new max.
      unit_time_secs = unit_stop_time.tv_sec - unit_start_time.tv_sec;
      if (slowest_unit_time_secs < unit_time_secs)
        slowest_unit_time_secs = unit_time_secs;

      free(copy);
    }
  }
  fprintf(stderr, "%s: successfully executed %d input(s)\n", argv[0], num_runs);
}
