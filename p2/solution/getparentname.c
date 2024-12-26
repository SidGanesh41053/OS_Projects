#include "types.h"
#include "stat.h"
#include "user.h"

int main(void) {
    char parentbuf[16];
    char childbuf[16];

    // correct buffer size test
    printf(1, "Test 1: Correct buffer sizes\n");
    if (getparentname(parentbuf, childbuf, 16, 16) == 0) {
        printf(1, "XV6_TEST_OUTPUT Parent name: %s Child name: %s\n", parentbuf, childbuf);
    } else {
        printf(1, "Error: getparentname failed\n");
    }

    // truncation buffer test
    printf(1, "Test 2: Small buffer sizes (truncation)\n");
    char smallparentbuf[4];
    char smallchildbuf[4];
    if (getparentname(smallparentbuf, smallchildbuf, 4, 4) == 0) {
        printf(1, "XV6_TEST_OUTPUT Parent name: %s Child name: %s\n", smallparentbuf, smallchildbuf);
    } else {
        printf(1, "Error: getparentname failed\n");
    }

    // mismatched parent and child buffer sizes
    printf(1, "Test 3: Mismatched buffer sizes\n");
    if (getparentname(parentbuf, smallchildbuf, 16, 4) == 0) {
        printf(1, "XV6_TEST_OUTPUT Parent name: %s Child name: %s\n", parentbuf, smallchildbuf);
    } else {
        printf(1, "Error: getparentname failed\n");
    }

    exit();
}
