# CS537 Fall 2024, Project 3

In this assignment, I wrote a Command Line Interpreter (Shell).

---

## My Information

- **Name**: Sid Ganesh
- **CS Login**: sganesh
- **Wisc ID and Email**: sganesh9@wisc.edu
- **Status of Implementation**: Working as intended

---

## Implementation Description

### 1. Simple implementation first
Initially, as the writeup suggested, I implemented the basic functionality first. I made sure the main function could parse the argv array to determine whether we were running in interactive mode or batch mode:
- Interactive Mode: No additional arguments, meaning the user types commands directly.
- Batch Mode: An input file is provided, and the shell reads commands from the file.
At this stage, I added the ability to run basic commands such as ls, ps, etc., through the use of a child process and execv(). Even though ls would eventually be a built-in command, my focus here was on correctly parsing input and executing commands in general. The goal was to confirm that I could parse inputs properly and execute commands as expected. strtok() and execv() were a bit hard to understand at first, but after looking up the relevant man pages and some video tutorials (see resources.txt), I was able to understand how they worked, whether it was making sure that the last element passed in the array to execv had to have a null pointer, or whether we could continue to the next delimited part of the command by passing null into strtok and the delimiter.

Built-in Commands Implementation

Next, I worked on implementing the built-in commands as specified in the requirements:
- exit: mostly straightforward, requiring a simple check for arguments to ensure none were passed (since it is an error to pass any). If arguments were detected, the shell responded with an error message.
- cd: required careful handling to ensure it took exactly one argument, with checks to prevent misuse. I used chdir() for changing directories and added appropriate error messages for incorrect usage or invalid paths.
- export and local: export command assigns environment variables, while local manages shell variables. I implemented a linked list to manage shell variables, based on Piazza suggestions. This required functions for setting, getting, and freeing variables. I had some trouble with string parsing here, so I occassionally used outside resources such as LLMs to help me parse the input and correctly set.
- vars: prints all current local shell variables. Required traversal of the linked list and correct formatting for output.
- ls: since ls is implemented as a built-in command (LANG=C ls -1 --color=never), it was important to replicate the exact behavior. To achieve this:
I opened the current directory and used readdir() to read and store the contents. The directory contents were then sorted alphabetically, similar to how the default ls behaves, so that my output matched exactly to the expected behavior out of the test

- Command History: implementing the command history required storing the last n commands. I had to make sure that no consecutive duplicate commands were stored, and built-in commands (such as history, cd, exit, etc.) were not added to the history. To store command history, I created two structs: one struct represented a command, and the other struct had an attribute for a list of commands. One key functionality of the history command was for users to be able to reexecute commands from the history. Initially, I had some errors understanding how to do this, so I resorted to using the same process of creating a child process and using execv to run the command (since we already had it stored, name-wise). If there was a better way to do this, I'd love to learn more. Additionally, like I mentioned above, we wanted it so the length of commands stored was configurable. Whenever I would reset the size, though, all my previous commands would be lost. So, to fix this I modified the logic to copy over the existing commands that would still fit into the new history capacity. This required allocating new memory for the resized command list and correctly freeing any commands that would no longer be stored due to the reduced capacity. By implementing this, I ensured that resizing the history (both increasing and decreasing) worked smoothly without losing valuable commands unnecessarily.

- Redirection: this was by far the hardest part of the project for me, and required the most use of outside resources, such as video tutorials, StackOverflow, W3Schools and LLMs. I found it challenging to understand the different types of redirections (e.g., input, output, error redirections, and their variations like appending). Implementing features like >, >>, 2>, and &> was complex because each redirection has unique requirements for file handling and standard file descriptors. To implement these features effectively, I needed to understand how file descriptors work, specifically how to use dup2() to redirect output to different files. For instance, > filename involved opening the file in write mode, possibly truncating it, and then using dup2() to ensure subsequent writes by the command went to that file. Similarly, >> filename required opening the file in append mode, while 2> was used to redirect stderr to a file. Initially, I had bugs where file descriptors weren’t being closed properly, causing unintended behaviors when executing multiple commands in a row.  For combined redirections like &>, I realized that both stdout and stderr needed to be directed to the same file descriptor. This involved opening the output file and duplicating the file descriptor to both STDOUT_FILENO and STDERR_FILENO, ensuring both streams were written to the specified file.

### Testing
- Tests 6, 11, and 13 in particular gave me the most issues. 6 was failing because I hadn't implemented a target in my Makefile that created a wsh-asan script, so the output of the ls command was not matching the actual directory's contents. For 13 and 11, they were failing mostly because of return code and .err. Return code expected for 11 was 255, so instead of exiting with -1, I exited with 255 wherever necessary (since per Piazza, as long as it was non-zero we were fine). Additionally, both write to standard error in the way that I have implemented, so I had to edit 11.run to ../solution/wsh <tests/11.wsh 2>/dev/null as stated in Piazza.

### Final
- At the end, once I passed all the tests, I realized I had all of my files in wsh.c only and my wsh.h was empty still. So, I quickly defined the function prototypes and struct macros, and used some help from LLMs to make sure that I was defining my guards correctly, such as #ifndef WSH_H, #define WSH_H, and #endif // WSH_H. From the LLM I learned that these should be defined to avoid redefinition errors. Overall, the project was very enjoyable, and I ended up using one slip day and completing the project on Friday, October 11. My code review is scheduled for next Friday, October 18.
