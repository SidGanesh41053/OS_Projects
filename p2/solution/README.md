# CS537 Fall 2024, Project 2

In this assignment, I created a new system call in the xv6 OS. The getparentname system call we had to implement retrieves the name of the current process and its parent process, copying them into user-provided buffers. It handles truncation safely if the buffer is too small to hold the process names.

---

## My Information

- **Name**: Sid Ganesh
- **CS Login**: sganesh
- **Wisc ID and Email**: sganesh9@wisc.edu
- **Status of Implementation**: Working as intended

---

## Implementation Description

### Files Modified
#### syscall.c
Added an entry to the syscalls[] array that maps the system call number SYS_getparentname to my handler function sys_getparentname()

#### syscall.h
Defined new system call number for my system call (first available open one was 22)

#### sysproc.c
Defined the system call here

#### defs.h
Added the function prototype for my system call kernel function

#### user.h
Added the prototype for the getparentname system call here, which will be called by the user programs. This allows my user program to know that the getparentname system call exists, and how to invoke it

#### usys.S
Added a macro that generates the necessary assembly code to trigger a system call interrupt (trap) and switch from user mode to kernel mode, allowing the kernel to handle the system call

#### Makefile
Added _getparentname to the list of user programs (UPROGS) to be compiled and included in the xv6 file system

## Testing
I wrote two extra test cases on top of testing in a user program.
- Truncation Test: Tests if the system call truncates the child process name when a smaller buffer is provided.
- Mismatched Buffer Sizes: Tests behavior when the buffer for the child name is smaller than the buffer for the parent name.

## Struggles
Overall, this project was a struggle because I did not know much about what we were doing. The majority of me knowing which files to edit came from https://www.geeksforgeeks.org/xv6-operating-system-adding-a-new-system-call/ as well as running grep -r "chdir" and grep -r "getpid" so that I knew based on these system calls and the files they are defined in, which files I should modify. Furthermore, in terms of handling truncation and buffer overflows, I used safestrcpy instead of normal strcpy which is defined in string.c. I was scrolling through definitions and found that safestrcpy was defined in a way that could help me simplify the implementation of the system call in sysproc.c. One more big issue I had was with making my own tests. For some reason, even though the .out files for my own defined tests matched exactly as the ones outputted after running the test script in tests-out/, I was getting an error that said they didn't match. So, I had to use ChatGPT to diagnose this error, and I found that in the .out files generated from the shell script, there was a ^M character at the end, but when I touched my own .out file in tests/, this ^M file was not there. So, the solution that ChatGPT gave me was to run this command to manually add the ^M character to the end of my files so that they truly matched, and this ended up working. However, I did not have this issue in the previous project, so if I was doing something wrong, I'd love some feedback from a TA. [sed 's/$/\r/' X.out > new_X.out; mv new_X.out X.out]. Overall, the bulk of the complexity was in defining the system call and its implementation in sysproc.c, all the other files' modifications were straightforward/copy and pasting another line and editing it slightly. (Also, see resources.txt)
