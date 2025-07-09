#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

#define MAX_TRAINS 75

// Train struct
typedef struct {
    int id;
    bool high_priority;
    bool direction_east;
    int load_tenths_sec;
    int cross_tenths_sec;
    struct timespec ready_time;
} train_t;

// Linked-list node for ready queues
typedef struct train_node {
    train_t *train;
    struct train_node *next;
} train_node_t;

// Globals
static train_t trains[MAX_TRAINS];
static int train_count = 0;
static train_node_t *east_head = NULL, *east_tail = NULL;
static train_node_t *west_head = NULL, *west_tail = NULL;
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t track_free_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t train_cond[MAX_TRAINS];
static bool can_cross[MAX_TRAINS] = {false};
static bool track_busy = false;
static int last_dir = -1;
static int same_count = 0;
static int done_count = 0;
static struct timespec start_time;

// Sleep helper
static void sleep_tenths(int t) {
    struct timespec req = { .tv_sec = t/10, .tv_nsec = (t%10)*100000000L };
    nanosleep(&req, NULL);
}

// Timestamp print
static void print_timestamp(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long ds = now.tv_sec - start_time.tv_sec;
    long dn = now.tv_nsec - start_time.tv_nsec;
    if (dn < 0) { ds--; dn += 1000000000L; }
    long tenths = ds*10 + (dn+50000000L)/100000000L;
    int h = tenths/36000;
    int m = (tenths%36000)/600;
    int s = (tenths%600)/10;
    int d = tenths%10;
    printf("%02d:%02d:%02d.%1d ", h, m, s, d);
}

// Thread-safe print
static void ts_printf(const char *fmt, ...) {
    va_list ap;
    pthread_mutex_lock(&output_mutex);
    print_timestamp();
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
    pthread_mutex_unlock(&output_mutex);
}

// Queue ops
static void enqueue(train_t *t) {
    train_node_t *n = malloc(sizeof(*n)); n->train = t; n->next = NULL;
    if (t->direction_east) {
        if (!east_tail) east_head = n; else east_tail->next = n;
        east_tail = n;
    } else {
        if (!west_tail) west_head = n; else west_tail->next = n;
        west_tail = n;
    }
}
static train_node_t* find_first(train_node_t *h, bool high) {
    for (train_node_t *p = h; p; p = p->next)
        if (p->train->high_priority == high)
            return p;
    return NULL;
}
static void remove_id(int id) {
    train_node_t **ph = trains[id].direction_east ? &east_head : &west_head;
    train_node_t **pt = trains[id].direction_east ? &east_tail : &west_tail;
    while (*ph) {
        if ((*ph)->train->id == id) {
            train_node_t *tmp = *ph;
            *ph = tmp->next;
            if (!*ph) *pt = NULL;
            free(tmp);
            return;
        }
        ph = &(*ph)->next;
    }
}

// Scheduler helper
static int pick_train(void) {
    if (same_count >= 2 && last_dir != -1) {
        int opp = 1 - last_dir;
        train_node_t *q = opp ? east_head : west_head;
        if (q) return q->train->id;
    }
    train_node_t *hE = find_first(east_head, true);
    train_node_t *hW = find_first(west_head, true);
    if (hE||hW) {
        if (hE&&hW) {
            int pick = (last_dir==-1?0:1-last_dir);
            return pick? hE->train->id: hW->train->id;
        }
        return hE? hE->train->id: hW->train->id;
    }
    train_node_t *lE = find_first(east_head, false);
    train_node_t *lW = find_first(west_head, false);
    if (lE&&lW) {
        int pick = (last_dir==-1?0:1-last_dir);
        return pick? lE->train->id: lW->train->id;
    }
    if (lE) return lE->train->id;
    if (lW) return lW->train->id;
    return -1;
}

// Thread function
static void* train_thread(void *arg) {
    train_t *t = arg;
    sleep_tenths(t->load_tenths_sec);
    clock_gettime(CLOCK_MONOTONIC, &t->ready_time);
    ts_printf("Train %2d is ready to go %4s\n", t->id,
              t->direction_east?"East":"West");
    pthread_mutex_lock(&state_mutex);
    enqueue(t);
    pthread_cond_broadcast(&ready_cond);
    while (!can_cross[t->id])
        pthread_cond_wait(&train_cond[t->id], &state_mutex);
    pthread_mutex_unlock(&state_mutex);
    ts_printf("Train %2d is ON the main track going %4s\n", t->id,
              t->direction_east?"East":"West");
    sleep_tenths(t->cross_tenths_sec);
    ts_printf("Train %2d is OFF the main track after going %4s\n", t->id,
              t->direction_east?"East":"West");
    pthread_mutex_lock(&state_mutex);
    track_busy = false;
    done_count++;
    pthread_cond_signal(&track_free_cond);
    pthread_cond_broadcast(&ready_cond);
    pthread_mutex_unlock(&state_mutex);
    return NULL;
}

// Scheduler
static void scheduler(void) {
    pthread_mutex_lock(&state_mutex);
    while (done_count < train_count) {
        while (!east_head && !west_head && done_count < train_count)
            pthread_cond_wait(&ready_cond, &state_mutex);
        while (track_busy)
            pthread_cond_wait(&track_free_cond, &state_mutex);
        int id = pick_train();
        if (id>=0) {
            remove_id(id);
            can_cross[id] = true;
            track_busy = true;
            int dir = trains[id].direction_east?1:0;
            if (last_dir==dir) same_count++;
            else { last_dir=dir; same_count=1; }
            pthread_cond_signal(&train_cond[id]);
        }
    }
    pthread_mutex_unlock(&state_mutex);
}

// Main
int main(int argc, char **argv){
    if(argc!=2){fprintf(stderr,"Usage: %s <in>\n",argv[0]);return EXIT_FAILURE;}
    FILE *f = fopen(argv[1],"r");if(!f){perror("fopen");return EXIT_FAILURE;}
    char d; while(fscanf(f," %c %d %d",&d,
        &trains[train_count].load_tenths_sec,
        &trains[train_count].cross_tenths_sec)==3){
        trains[train_count].id = train_count;
        trains[train_count].high_priority=(d=='E'||d=='W');
        trains[train_count].direction_east=(d=='e'||d=='E');
        train_count++;
    }
    fclose(f);
    clock_gettime(CLOCK_MONOTONIC,&start_time);
    // initialize condition vars
    for(int i=0;i<train_count;i++) pthread_cond_init(&train_cond[i],NULL);
    pthread_t threads[MAX_TRAINS];
    for(int i=0;i<train_count;i++){
        pthread_create(&threads[i],NULL,train_thread,&trains[i]);
    }
    scheduler();
    for(int i=0;i<train_count;i++) pthread_join(threads[i],NULL);
    return EXIT_SUCCESS;
}
