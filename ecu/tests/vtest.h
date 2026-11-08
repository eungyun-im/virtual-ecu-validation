/* Minimal unit test framework: TEST() functions in a table, CHECK() records failures,
 * --junit <file> writes a JUnit XML report that tools/rtm.py and tools/test_report.py read.
 * Test names carry the requirement ID (test_REQ_04_...), which is how the RTM links them. */
#ifndef VTEST_H
#define VTEST_H

#include <stdio.h>
#include <string.h>

typedef struct {
  const char *name;
  void (*fn)(void);
} vt_case_t;

static int vt_fail_count;
static char vt_msg[512];

#define CHECK(c)                                                                                \
  do {                                                                                          \
    if (!(c)) {                                                                                 \
      if (vt_fail_count == 0) snprintf(vt_msg, sizeof vt_msg, "%s:%d: %s", __FILE__, __LINE__, #c); \
      ++vt_fail_count;                                                                          \
    }                                                                                           \
  } while (0)
#define CHECK_EQ(a, b)                                                                          \
  do {                                                                                          \
    long long va_ = (long long)(a), vb_ = (long long)(b);                                       \
    if (va_ != vb_) {                                                                           \
      if (vt_fail_count == 0)                                                                   \
        snprintf(vt_msg, sizeof vt_msg, "%s:%d: %s == %s (%lld != %lld)", __FILE__, __LINE__, #a, #b, va_, vb_); \
      ++vt_fail_count;                                                                          \
    }                                                                                           \
  } while (0)

static int vt_main(int argc, char **argv, const vt_case_t *cases, int n, const char *suite)
{
  int i, failed = 0;
  FILE *j = NULL;
  if (argc == 3 && strcmp(argv[1], "--junit") == 0) {
    j = fopen(argv[2], "w");
  }
  if (j) fprintf(j, "<?xml version=\"1.0\"?>\n<testsuite name=\"%s\" tests=\"%d\">\n", suite, n);
  for (i = 0; i < n; ++i) {
    vt_fail_count = 0;
    vt_msg[0] = '\0';
    cases[i].fn();
    printf("%s %s.%s%s%s\n", vt_fail_count ? "FAIL" : "PASS", suite, cases[i].name, vt_fail_count ? "  " : "", vt_msg);
    if (j) {
      fprintf(j, "  <testcase classname=\"unit.%s\" name=\"%s\">", suite, cases[i].name);
      if (vt_fail_count) fprintf(j, "<failure message=\"%s\"/>", vt_msg);
      fprintf(j, "</testcase>\n");
    }
    failed += vt_fail_count ? 1 : 0;
  }
  if (j) {
    fprintf(j, "</testsuite>\n");
    fclose(j);
  }
  printf("%s: %d/%d passed\n", suite, n - failed, n);
  return failed ? 1 : 0;
}

#endif
