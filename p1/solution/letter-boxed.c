// valgrind --track-origins=yes --leak-check=full ./letter-boxed

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE_LENGTH 27 // 26 alphabet + 1 for null char
#define MAX_WORD_LENGTH 100
#define ALPHABET_SIZE 26

// Check if a word exists in the dictionary
int word_in_dictionary(const char *word, char **dictionary, int dict_size) {
    for (int i = 0; i < dict_size; i++) {
        if (strcmp(word, dictionary[i]) == 0) {
            return 1;  // Word found in the dictionary
        }
    }
    return 0;
}

// Check if a letter exists on the board
int letter_on_board(char letter, char **board, int num_sides) {
    for (int i = 0; i < num_sides; i++) {
        if (strchr(board[i], letter)) {
            return 1;  // Letter found on the board
        }
    }
    return 0;
}

// Which side of the board a letter is on
int get_side_of_letter(char letter, char **board, int num_sides) {
    for (int i = 0; i < num_sides; i++) {
        if (strchr(board[i], letter)) {
            return i;  // Return the side index of the letter
        }
    }
    return -1;
}

// Duplicate a string
char *string_duplicate(const char *source) {
    char *duplicate = malloc(strlen(source) + 1); // +1 for the null char \0 ?
    if (duplicate != NULL) {
        strcpy(duplicate, source);
    }
    return duplicate;
}

// Check if the board contains repeated letters
int has_repeated_letters(char **board, int num_sides) {
    int letter_flag[ALPHABET_SIZE] = {0}; // letter occurrences, 0 unused, 1 used
    for (int i = 0; i < num_sides; i++) {
        for (size_t j = 0; j < strlen(board[i]); j++) {
            int index = board[i][j] - 'a';  // Get index of the current letter (subtracting ascii vals)
            if (letter_flag[index] == 1) {
                return 1;
            }
            letter_flag[index] = 1; // letter is used
        }
    }
    return 0; 
}

// Parsing board
char **parse_board(const char *filename, int *num_sides) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        printf("Error: Could not open board file.\n");
        exit(1);
    }

    char **board = NULL;
    char line[MAX_LINE_LENGTH];
    *num_sides = 0;

    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = 0;  // remove \n
        
        if (strlen(line) == 0) {
            continue;
        }

        board = realloc(board, (*num_sides + 1) * sizeof(char *)); // would've used malloc, but board size unknown
        if (board == NULL) {
            printf("Error: Memory allocation failed.\n");
            exit(1);
        }
        board[*num_sides] = string_duplicate(line);
        (*num_sides)++;
    }

    fclose(file);
    return board;
}

// Load dictionary
char **load_dictionary(const char *filename, int *dict_size) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        printf("Error: Could not open dictionary file.\n");
        exit(1);
    }

    char **dictionary = NULL;
    char word[MAX_WORD_LENGTH];
    *dict_size = 0;

    while (fgets(word, sizeof(word), file)) {
        word[strcspn(word, "\n")] = 0;

        // Dynamically allocate memory for the new word
        dictionary = realloc(dictionary, (*dict_size + 1) * sizeof(char *));
        if (dictionary == NULL) {
            printf("Error: Memory allocation failed.\n");
            exit(1);
        }
        dictionary[*dict_size] = string_duplicate(word);
        (*dict_size)++;
    }

    fclose(file);
    return dictionary;
}

// Freeing all heap allocated memory helper func
void free_board_and_dictionary(char **board, int num_sides, char **dictionary, int dict_size) {
    for (int i = 0; i < num_sides; i++) {
        free(board[i]);
    }
    free(board);

    for (int i = 0; i < dict_size; i++) {
        free(dictionary[i]);
    }
    free(dictionary);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <board_file> <dictionary_file>\n", argv[0]);
        return 1;
    }

    // load board & dict
    int num_sides;
    char **board = parse_board(argv[1], &num_sides);
    int dict_size;
    char **dictionary = load_dictionary(argv[2], &dict_size);

    // Phase 1: check if the board has fewer than 3 sides
    if (num_sides < 3) {
        printf("Invalid board\n");
        free_board_and_dictionary(board, num_sides, dictionary, dict_size);
        return 1;
    }

    // Phase 1: check if the board has any repeated letters
    if (has_repeated_letters(board, num_sides)) {
        printf("Invalid board\n");
        free_board_and_dictionary(board, num_sides, dictionary, dict_size);
        return 1;
    }

    int used_letters[ALPHABET_SIZE] = {0};  // tracks used board letters
    char prev_last_char = '\0';             // tracks last char from prev word
    char word[MAX_WORD_LENGTH];

    while (scanf("%s", word) != EOF) {
        int side_used = -1;  // reset side used at new word occurrence

        // Phase 2: check if each letter in the word is present on the board
        for (size_t i = 0; i < strlen(word); i++) {
            char letter = word[i];
            if (!letter_on_board(letter, board, num_sides)) {
                printf("Used a letter not present on the board\n");
                free_board_and_dictionary(board, num_sides, dictionary, dict_size);
                return 0;
            }
        }

        // Phase 2: check if the first letter of the word matches the last letter of the previous word
        if (prev_last_char != '\0' && word[0] != prev_last_char) {
            printf("First letter of word does not match last letter of previous word\n");
            free_board_and_dictionary(board, num_sides, dictionary, dict_size);
            return 0;
        }

        // Phase 2: check that no same side letters are used consecutively
        for (size_t i = 0; i < strlen(word); i++) {
            char letter = word[i];
            int current_side = get_side_of_letter(letter, board, num_sides);
            if (side_used != -1 && current_side == side_used) {
                printf("Same-side letter used consecutively\n");
                free_board_and_dictionary(board, num_sides, dictionary, dict_size);
                return 0;
            }
            used_letters[letter - 'a'] = 1;
            side_used = current_side;
        }

        // Phase 2: check if the word is in the dictionary
        if (!word_in_dictionary(word, dictionary, dict_size)) {
            printf("Word not found in dictionary\n");
            free_board_and_dictionary(board, num_sides, dictionary, dict_size);
            return 0;
        }
        
        prev_last_char = word[strlen(word) - 1];
    }

    // Phase 2: check if all letters on the board have been used
    for (int i = 0; i < num_sides; i++) {
        for (size_t j = 0; j < strlen(board[i]); j++) {
            if (!used_letters[board[i][j] - 'a']) {
                printf("Not all letters used\n");
                free_board_and_dictionary(board, num_sides, dictionary, dict_size);
                return 0;
            }
        }
    }

    printf("Correct\n");
    free_board_and_dictionary(board, num_sides, dictionary, dict_size);
    return 0;
}