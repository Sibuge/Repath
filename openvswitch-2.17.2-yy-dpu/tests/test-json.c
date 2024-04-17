/*
 * Copyright (c) 2009, 2010, 2014 Nicira, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>
#undef NDEBUG
#include "openvswitch/json.h"
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include "ovstest.h"
#include "random.h"
#include "timeval.h"
#include "util.h"

/* --pretty: If set, the JSON output is pretty-printed, instead of printed as
 * compactly as possible.  */
static int pretty = 0;

/* --multiple: If set, the input is a sequence of JSON objects or arrays,
 * instead of exactly one object or array. */
static int multiple = 0;

static bool
print_and_free_json(struct json *json)
{
    bool ok;
    if (json->type == JSON_STRING) {
        printf("error: %s\n", json->string);
        ok = false;
    } else {
        char *s = json_to_string(json, JSSF_SORT | (pretty ? JSSF_PRETTY : 0));
        puts(s);
        free(s);
        ok = true;
    }
    json_destroy(json);
    return ok;
}

static bool
refill(FILE *file, void *buffer, size_t buffer_size, size_t *n, size_t *used)
{
    *used = 0;
    if (feof(file)) {
        *n = 0;
        return false;
    } else {
        *n = fread(buffer, 1, buffer_size, file);
        if (ferror(file)) {
            ovs_fatal(errno, "Error reading input file");
        }
        return *n > 0;
    }
}

static bool
parse_multiple(FILE *stream)
{
    struct json_parser *parser;
    char buffer[BUFSIZ];
    size_t n, used;
    bool ok;

    parser = NULL;
    n = used = 0;
    ok = true;
    while (used < n || refill(stream, buffer, sizeof buffer, &n, &used)) {
        if (!parser && isspace((unsigned char) buffer[used])) {
            /* Skip white space. */
            used++;
        } else {
            if (!parser) {
                parser = json_parser_create(0);
            }

            used += json_parser_feed(parser, &buffer[used], n - used);
            if (used < n) {
                if (!print_and_free_json(json_parser_finish(parser))) {
                    ok = false;
                }
                parser = NULL;
            }
        }
    }
    if (parser) {
        if (!print_and_free_json(json_parser_finish(parser))) {
            ok = false;
        }
    }
    return ok;
}

static void
test_json_main(int argc, char *argv[])
{
    const char *input_file;
    FILE *stream;
    bool ok;

    set_program_name(argv[0]);

    for (;;) {
        static const struct option options[] = {
            {"pretty", no_argument, &pretty, 1},
            {"multiple", no_argument, &multiple, 1},
        };
        int option_index = 0;
        int c = getopt_long (argc, argv, "", options, &option_index);

        if (c == -1) {
            break;
        }
        switch (c) {
        case 0:
            break;

        case '?':
            exit(1);

        default:
            abort();
        }
    }

    if (argc - optind != 1) {
        ovs_fatal(0, "usage: %s [--pretty] [--multiple] INPUT.json",
                  program_name);
    }

    input_file = argv[optind];
    stream = !strcmp(input_file, "-") ? stdin : fopen(input_file, "r");
    if (!stream) {
        ovs_fatal(errno, "Cannot open \"%s\"", input_file);
    }

    if (multiple) {
        ok = parse_multiple(stream);
    } else {
        ok = print_and_free_json(json_from_stream(stream));
    }

    fclose(stream);

    exit(!ok);
}

OVSTEST_REGISTER("test-json", test_json_main);

static void
json_string_benchmark_main(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
    struct {
        int n;
        int quote_probablility;
        int special_probability;
        int iter;
    } configs[] = {
        { 100000,     0, 0, 1000, },
        { 100000,     2, 1, 1000, },
        { 100000,    10, 1, 1000, },
        { 10000000,   0, 0, 100,  },
        { 10000000,   2, 1, 100,  },
        { 10000000,  10, 1, 100,  },
        { 100000000,  0, 0, 10.   },
        { 100000000,  2, 1, 10,   },
        { 100000000, 10, 1, 10,   },
    };

    printf("  SIZE      Q  S            TIME\n");
    printf("--------------------------------------\n");

    for (int i = 0; i < ARRAY_SIZE(configs); i++) {
        int iter = configs[i].iter;
        int n = configs[i].n;
        char *str = xzalloc(n);

        for (int j = 0; j < n - 1; j++) {
            int r = random_range(100);

            if (r < configs[i].special_probability) {
                str[j] = random_range(' ' - 1) + 1;
            } else if (r < (configs[i].special_probability
                            + configs[i].quote_probablility)) {
                str[j] = '"';
            } else {
                str[j] = random_range(256 - ' ') + ' ';
            }
        }

        printf("%-11d %-2d %-2d: ", n, configs[i].quote_probablility,
                                       configs[i].special_probability);
        fflush(stdout);

        struct json *json = json_string_create_nocopy(str);
        uint64_t start = time_msec();

        char **res = xzalloc(iter * sizeof *res);
        for (int j = 0; j < iter; j++) {
            res[j] = json_to_string(json, 0);
        }

        printf("%16.3lf ms\n", (double) (time_msec() - start) / iter);
        json_destroy(json);
        for (int j = 0; j < iter; j++) {
            free(res[j]);
        }
        free(res);
    }

    exit(0);
}

OVSTEST_REGISTER("json-string-benchmark", json_string_benchmark_main);
