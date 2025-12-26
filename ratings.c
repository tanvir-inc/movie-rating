#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>     // usleep
#include <stdlib.h>     // qsort

#define MAX_TITLE_LEN    100
#define MAX_DIRECTOR_LEN 100
#define MAX_DATE_LEN     20
#define MAX_DESC_LEN     500

#define MAX_MOVIES       30
#define MAX_RESULTS      30
#define MAX_KEYWORD_LEN  64

typedef struct {
    char title[MAX_TITLE_LEN];
    char director[MAX_DIRECTOR_LEN];
    char release_date[MAX_DATE_LEN];
    float popularity_rating;
    char description[MAX_DESC_LEN];
} Movie;

/* ----- Global “Database” (read-only) ----- */
static Movie g_movies[MAX_MOVIES];
static int g_movie_count = 0;

/* ----- Concurrency Controls ----- */
static sem_t g_db_sem;                 // counting semaphore, init to 5
static pthread_mutex_t g_print_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ----- Utility: lowercase copy ----- */
static void to_lower_copy(char *dst, size_t dst_sz, const char *src) {
    size_t i = 0;
    for (; i + 1 < dst_sz && src[i] != '\0'; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

/* ----- Utility: case-insensitive substring check ----- */
static int contains_keyword_ci(const char *text, const char *keyword) {
    if (!text || !keyword) return 0;
    if (keyword[0] == '\0') return 0;

    char text_l[MAX_DESC_LEN];
    char key_l[MAX_KEYWORD_LEN];

    to_lower_copy(text_l, sizeof(text_l), text);
    to_lower_copy(key_l, sizeof(key_l), keyword);

    return (strstr(text_l, key_l) != NULL);
}

/* ----- Sorting results by rating (descending) using indices ----- */
typedef struct {
    int idx;        // index into g_movies
    float rating;
} ResultItem;

static int cmp_result_desc(const void *a, const void *b) {
    const ResultItem *ra = (const ResultItem *)a;
    const ResultItem *rb = (const ResultItem *)b;

    if (ra->rating < rb->rating) return 1;   // descending
    if (ra->rating > rb->rating) return -1;
    return 0;
}

/* ----- Search function: scans DB, filters, sorts, prints ----- */
static void search_movies_and_print(const char *keyword) {
    ResultItem results[MAX_RESULTS];
    int res_count = 0;

    // Scan database
    for (int i = 0; i < g_movie_count; i++) {
        if (contains_keyword_ci(g_movies[i].description, keyword)) {
            if (res_count < MAX_RESULTS) {
                results[res_count].idx = i;
                results[res_count].rating = g_movies[i].popularity_rating;
                res_count++;
            }
        }
    }

    // Sort matches by rating descending
    qsort(results, (size_t)res_count, sizeof(ResultItem), cmp_result_desc);

    // Print results (keep output clean)
    pthread_mutex_lock(&g_print_mutex);

    printf("\n============================================================\n");
    printf("Keyword: \"%s\" | Matches: %d\n", keyword, res_count);
    printf("Sorted by popularity rating (descending)\n");
    printf("------------------------------------------------------------\n");

    if (res_count == 0) {
        printf("(No matches found)\n");
    } else {
        for (int i = 0; i < res_count; i++) {
            const Movie *m = &g_movies[results[i].idx];
            printf("%2d) %-28s | %-18s | %s  (%.1f)\n",
                   i + 1, m->title, m->director, m->release_date, m->popularity_rating);
        }
    }

    printf("============================================================\n");
    pthread_mutex_unlock(&g_print_mutex);
}

/* ----- Thread worker ----- */
typedef struct {
    char keyword[MAX_KEYWORD_LEN];
    int worker_id;
} WorkerArgs;

static void *search_worker(void *arg) {
    WorkerArgs *wa = (WorkerArgs *)arg;

    // Log waiting
    pthread_mutex_lock(&g_print_mutex);
    printf("[Worker %d] Waiting for DB slot... (keyword: %s)\n", wa->worker_id, wa->keyword);
    pthread_mutex_unlock(&g_print_mutex);

    // Limit concurrent DB searches to 5
    sem_wait(&g_db_sem);

    pthread_mutex_lock(&g_print_mutex);
    printf("[Worker %d] Acquired DB slot. Starting search...\n", wa->worker_id);
    pthread_mutex_unlock(&g_print_mutex);

    // Simulate load so concurrency is visible (optional)
    usleep(200000); // 200 ms

    // Do the actual search + print
    search_movies_and_print(wa->keyword);

    pthread_mutex_lock(&g_print_mutex);
    printf("[Worker %d] Finished search. Releasing DB slot.\n", wa->worker_id);
    pthread_mutex_unlock(&g_print_mutex);

    sem_post(&g_db_sem);
    return NULL;
}

/* ----- Load a small hard-coded DB ----- */
static void load_movies_hardcoded(void) {
    g_movie_count = 0;

    g_movies[g_movie_count++] = (Movie){
        "How to Train Your Dragon", "Chris Sanders", "2010-03-26", 92.5f,
        "A young Viking befriends a dragon and changes his village forever."
    };
    g_movies[g_movie_count++] = (Movie){
        "Dragonheart", "Rob Cohen", "1996-05-31", 71.0f,
        "A knight teams up with a dragon to overthrow a tyrant king."
    };
    g_movies[g_movie_count++] = (Movie){
        "Spirited Away", "Hayao Miyazaki", "2001-07-20", 97.0f,
        "A girl enters a spirit world filled with magic, mystery, and courage."
    };
    g_movies[g_movie_count++] = (Movie){
        "Interstellar", "Christopher Nolan", "2014-11-07", 89.0f,
        "A space mission searches for a new home for humanity beyond Earth."
    };
    g_movies[g_movie_count++] = (Movie){
        "The Dark Knight", "Christopher Nolan", "2008-07-18", 94.0f,
        "A crime saga where Gotham faces chaos and a villain tests the hero."
    };
    g_movies[g_movie_count++] = (Movie){
        "Inception", "Christopher Nolan", "2010-07-16", 91.0f,
        "A thief enters dreams to plant an idea; reality becomes uncertain."
    };
    g_movies[g_movie_count++] = (Movie){
        "The Lord of the Rings", "Peter Jackson", "2001-12-19", 96.0f,
        "An epic war against darkness with magic, courage, and sacrifice."
    };
    g_movies[g_movie_count++] = (Movie){
        "Love Actually", "Richard Curtis", "2003-11-14", 78.0f,
        "Multiple stories of love unfold during the holiday season."
    };
    g_movies[g_movie_count++] = (Movie){
        "War Horse", "Steven Spielberg", "2011-12-25", 75.0f,
        "A boy and his horse are separated by war and struggle to reunite."
    };
    g_movies[g_movie_count++] = (Movie){
        "The Girl with the Dragon Tattoo", "David Fincher", "2011-12-21", 86.0f,
        "A journalist and hacker investigate a mystery with dark secrets."
    };
}

/* ----- Main driver ----- */
int main(void) {
    load_movies_hardcoded();

    // Initialize counting semaphore to 5 (max 5 concurrent searches)
    if (sem_init(&g_db_sem, 0, 5) != 0) {
        perror("sem_init failed");
        return 1;
    }

    // Create more than 5 searches to prove blocking works
    const char *keywords[] = {
        "dragon", "magic", "war", "love", "space", "crime", "dream", "mystery"
    };
    const int N = (int)(sizeof(keywords) / sizeof(keywords[0]));

    pthread_t threads[N];
    WorkerArgs args[N];

    for (int i = 0; i < N; i++) {
        args[i].worker_id = i + 1;
        strncpy(args[i].keyword, keywords[i], sizeof(args[i].keyword) - 1);
        args[i].keyword[sizeof(args[i].keyword) - 1] = '\0';

        if (pthread_create(&threads[i], NULL, search_worker, &args[i]) != 0) {
            perror("pthread_create failed");
            return 1;
        }
    }

    for (int i = 0; i < N; i++) {
        pthread_join(threads[i], NULL);
    }

    sem_destroy(&g_db_sem);
    pthread_mutex_destroy(&g_print_mutex);

    printf("\nAll searches finished.\n");
    return 0;
}
