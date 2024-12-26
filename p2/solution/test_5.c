#include "types.h"
#include "stat.h"
#include "user.h"

int main(void) {
    char parent_name[16];
    char child_name[4];

    if (getparentname(parent_name, child_name, 16, 4) == 0) {
        printf(1, "XV6_TEST_OUTPUT Parent name: %s Child name: %s\n", parent_name, child_name);
    } else {
        printf(1, "Error: getparentname failed\n");
    }
    exit();
}