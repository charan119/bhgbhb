#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <limits.h>

#define MAX_BUFFER_SIZE 256

struct MessageBuffer {
    long message_type;
    int key;
};

void decrypt_caesar(char *str, int key) {
    for (int i = 0; str[i] != '\0'; i++)
        str[i] = ((str[i] - 'a' + key) % 26) + 'a';
}

int count_word_occurrences(const char *word, char **word_list, int word_count) {
    int count = 0;
    for (int i = 0; i < word_count; i++) {
        const char *w1 = word, *w2 = word_list[i];
        while (*w1 && *w1 == *w2) { w1++; w2++; }
        if (!*w1 && !*w2) count++;
    }
    return count;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        exit(EXIT_FAILURE);
    }

    char input_file_name[256], word_file_name[256];
    sprintf(input_file_name, "input%s.txt", argv[1]);
    sprintf(word_file_name, "words%s.txt", argv[1]);

    FILE *input_file = fopen(input_file_name, "r");
    if (!input_file) {
        exit(EXIT_FAILURE);
    }

    int matrix_size, word_length;
    key_t shared_memory_key, message_queue_key;

    if (fscanf(input_file, "%d %d %d %d", &matrix_size, &word_length, &shared_memory_key, &message_queue_key) != 4) {
        fclose(input_file);
        exit(EXIT_FAILURE);
    }
    fclose(input_file);

    size_t shared_memory_size = sizeof(char[matrix_size][matrix_size][word_length]);
    int shared_memory_id = shmget(shared_memory_key, shared_memory_size, 0666);
    if (shared_memory_id == -1) {
        exit(EXIT_FAILURE);
    }

    char (*shared_matrix)[matrix_size][word_length] = shmat(shared_memory_id, NULL, 0);
    if (shared_matrix == (void *)-1) {
        exit(EXIT_FAILURE);
    }

    int message_queue_id = msgget(message_queue_key, 0666);
    if (message_queue_id == -1) {
        shmdt(shared_matrix);
        exit(EXIT_FAILURE);
    }

    FILE *word_file = fopen(word_file_name, "r");
    if (!word_file) {
        shmdt(shared_matrix);
        exit(EXIT_FAILURE);
    }

    char **word_list = malloc(sizeof(char *) * INT_MAX);
    if (!word_list) {
        fclose(word_file);
        shmdt(shared_matrix);
        exit(EXIT_FAILURE);
    }

    int total_words = 0;
    char buffer[MAX_BUFFER_SIZE];

    while (fscanf(word_file, "%255s", buffer) != EOF) {
        word_list[total_words] = malloc(strlen(buffer) + 1);
        if (!word_list[total_words]) {
            fclose(word_file);
            shmdt(shared_matrix);
            exit(EXIT_FAILURE);
        }
        strcpy(word_list[total_words], buffer);
        total_words++;
        if (total_words == INT_MAX) {
            fclose(word_file);
            shmdt(shared_matrix);
            exit(EXIT_FAILURE);
        }
    }
    fclose(word_file);

    int diagonal_count = 2 * matrix_size - 1, current_caesar_key = 0;
    for (int diag = 0; diag < diagonal_count; diag++) {
        int diagonal_word_count = 0;
        for (int i = 0; i < matrix_size; i++) {
            int j = diag - i;
            if (j >= 0 && j < matrix_size) {
                char decrypted_word[MAX_BUFFER_SIZE];
                strcpy(decrypted_word, shared_matrix[i][j]);
                decrypt_caesar(decrypted_word, current_caesar_key);
                diagonal_word_count += count_word_occurrences(decrypted_word, word_list, total_words);
            }
        }

        struct MessageBuffer send_message = {1, diagonal_word_count};
        if (msgsnd(message_queue_id, &send_message, sizeof(struct MessageBuffer) - sizeof(long), 0) == -1) {
            shmdt(shared_matrix);
            exit(EXIT_FAILURE);
        }

        struct MessageBuffer receive_message;
        if (msgrcv(message_queue_id, &receive_message, sizeof(struct MessageBuffer) - sizeof(long), 2, 0) == -1) {
            shmdt(shared_matrix);
            exit(EXIT_FAILURE);
        }

        current_caesar_key = receive_message.key % 26;
    }

    char *memory_block = calloc(total_words, word_length);
    for (int i = 0; i < total_words; i++)
        word_list[i] = memory_block + i * word_length;

    free(memory_block);
    free(word_list);
    shmdt(shared_matrix);

    return 0;
}
