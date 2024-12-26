# CS537 Fall 2024, Project 1

In this assignment, I wrote a program which checks solutions to the word game Letter Boxed.

---

## My Information

- **Name**: Sid Ganesh
- **CS Login**: sganesh
- **Wisc ID and Email**: sganesh9@wisc.edu
- **Status of Implementation**: Working as intended

---

## Implementation Description

### 1. Defining functions and modularizing as much as possible

I defined a function for basically every possible requirement for the program. I considered two phases to checking whether a board is correct: the board checking phase and the input stream checking case. The board checking phase consisted of checking whether or not the inputted board itself was valid. This involves seeing if the board had less than 3 sides or if the board repeated letters. Since the constraints of the problem stated that a board could have how many ever sides with each side having how many ever letters (so long as none are repeated), the board would be valid, so to accomodate this I dynamically allocated memory for the board parsing step until we had a board ready to use. Same goes for the dictionary file. The input stream checking phase consisted of checking whether or not the words were valid under the rules of the game, and also making sure that the correct errors were caught in the correct priority. My thought process for the priority of the errors being caught was as follows:
0. All the board parsing
1. Check if the letter is even in the board
2. If the current letter is the first of the word, then check if it matches the last of the previous word
3. Check if the letter used is in the same side as the previous letter
4. Check if the word we just made is even a word in the dictionary
5. Check if all the letters on the board are used finally
This order was very intuitive once I was hinted by the TA on Piazza @16 that we should think of it as if we were playing the game.

### 2. Creating tests

I created 9 extra tests that I believe tested the full range of edge cases. They should be in the tests folder with all relevant files for any given test as per the README.md file in the tests/ folder. All the tests pass in their current state.

### 3. Git Logistics

Just so all the new Git changes wouldn't show up on the left hand side of VSCode for me, I created a .gitignore file to ignore everything. I am hoping it does not mess with the submission in anyway, but if it does please reach out to me and I will delete it.
