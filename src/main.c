#include "mozvm.h"
#include "ast.h"
#include "loader.h"
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>

static void usage(const char *arg)
{
    fprintf(stderr, "Usage: %s -p <bytecode_file> -i <input_file>\n", arg);
}

static struct timeval g_timer;
static void reset_timer()
{
    gettimeofday(&g_timer, NULL);
}

static inline void _show_timer(const char *s, size_t bufsz)
{
    struct timeval endtime;
    gettimeofday(&endtime, NULL);
    double sec = (endtime.tv_sec - g_timer.tv_sec)
        + (double)(endtime.tv_usec - g_timer.tv_usec) / 1000 / 1000;
    printf("%20s: %f ms\n", s, sec * 1000.0);
    printf("%20s: %f MB\n", s, ((double)bufsz)/1024/1024);
    printf("%20s: %f Mbps\n", s, ((double)bufsz)*8/sec/1000/1000);
    printf("\n");
}

#if 0
static void show_timer(const char *s)
{
    struct timeval endtime;
    gettimeofday(&endtime, NULL);
    double sec = (endtime.tv_sec - g_timer.tv_sec)
        + (double)(endtime.tv_usec - g_timer.tv_usec) / 1000 / 1000;
    printf("%20s: %f sec\n", s, sec);
}
#endif

int main(int argc, char *const argv[])
{
    int parsed;
    mozvm_loader_t L = {};
    moz_inst_t *inst;

    const char *syntax_file = NULL;
    const char *input_file = NULL;
    unsigned tmp, loop = 1;
    unsigned print_stats = 0;
    unsigned quiet_mode = 0;
    int opt;

    while ((opt = getopt(argc, argv, "qsn:p:i:h")) != -1) {
        switch (opt) {
        case 'n':
            tmp = atoi(optarg);
            loop = tmp > loop ? tmp : loop;
            break;
        case 'q':
            quiet_mode = 1;
            break;
        case 's':
            print_stats = 1;
            break;
        case 'p':
            syntax_file = optarg;
            break;
        case 'i':
            input_file = optarg;
            break;
        case 'h':
        default: /* '?' */
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }
    if (syntax_file == NULL) {
        fprintf(stderr, "error: please specify bytecode file\n");
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    if (input_file == NULL) {
        fprintf(stderr, "error: please specify input file\n");
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    if (!mozvm_loader_load_input(&L, input_file)) {
        fprintf(stderr, "error: failed to load input_file='%s'\n", input_file);
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    inst = mozvm_loader_load_file(&L, syntax_file);
    assert(inst != NULL);
    while (loop-- > 0) {
        Node node = NULL;
        reset_timer();
        parsed = moz_runtime_parse(L.R, L.input, L.input + L.input_size, inst);
        if (parsed != 0) {
            fprintf(stderr, "parse error\n");
            break;
        }
        node = ast_get_parsed_node(L.R->ast);
        if (node) {
            if (!quiet_mode) {
                Node_print(node);
            }
            NODE_GC_RELEASE(node);
        }
        if (print_stats) {
            _show_timer(syntax_file, L.input_size);
        }
        moz_runtime_reset(L.R);
        NodeManager_reset();
    }
    moz_runtime_dispose(L.R);
    if (print_stats) {
        moz_loader_print_stats(&L);
        // moz_runtime_print_stats(L.R);
    }
    mozvm_loader_dispose(&L);
    NodeManager_dispose();
    return 0;
}
