#define WF_STANDALONE
#include "wf.h"

#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/socket.h>

#include <errno.h>

#ifdef WF_STANDALONE
#include <stdarg.h>
#include <string.h>
#endif // WF_STANDALONE

typedef struct Slice
{
    char *buf;
    int len;
} Slice;
typedef struct SlicePair
{
    Slice name, value;
} SlicePair;
#define SLICE(CLIT) \
    (Slice) { .buf = CLIT, .len = sizeof(CLIT) / sizeof(CLIT[0]) }

int slice_cmp(Slice a, Slice b) { return memcmp(a.buf, b.buf, a.len < b.len ? a.len : b.len); }
#define SLICE_FMT "%.*s"
#define SLICE_PNT(SLICE) SLICE.buf ? SLICE.len : (int)sizeof("(null)"), SLICE.buf ? SLICE.buf : "(null)"

int WriteBuffer(int fd, char *buffer, int bufferlen)
{
    int out = 0;
    do
    {
        int o = write(fd, buffer, bufferlen - out);
        if (0 > o)
        {
            perror("WriteBuffer");
            return o;
        }
        out += o;
    } while (bufferlen > out);

    // printf("TX'd %d/%d bytes to %d\n\'%s\'\n", out, bufferlen, fd, buffer);

    return 0;
}

char *HttpUrlStringSearch(char *buf, int *out_methodlen)
{
    // we read upto the first space
    char *ret = buf;

    for (*out_methodlen = 0;
         buf[*out_methodlen] &&
         buf[*out_methodlen] != ' ' &&
         buf[*out_methodlen] != '=' /* url value seprator */ &&
         buf[*out_methodlen] != '&' /* param seprator */ &&
         buf[*out_methodlen] != '?' /* url param sepprator */;
         *out_methodlen += 1)
        ;

    return ret;
}

/*
    returns NULL or body
*/
char *HttpGetBody(char *buf, int buflen, int *out_len)
{
    int bsi = 0;
    for (; bsi < buflen; bsi++)
    {
        if (buflen > bsi + 4 &&
            buf[bsi] == '\r' &&
            buf[bsi + 1] == '\n' &&
            buf[bsi + 2] == '\r' &&
            buf[bsi + 3] == '\n')
        {
            bsi += 4;
            goto found_body;
        }
    }

    *out_len = 0;
    return NULL;

found_body:
    *out_len = 0;
    char *bodystart = buf + bsi;
    *out_len = strlen(bodystart);
    return bodystart;
}

/* returns number of args found in buffer and saved to dst
    or -1 on error
*/
int HttpUrlGetArgs(char *buffer, int buflen, SlicePair *dst, int dstlen)
{
    // with args
    // "?something=yes HTTP/1.1"
    // without args
    // " HTTP/1.1"

    int nowlen, dstidx = 0;
    char *now = buffer;
    int kv = 0; /* toggles between 0 and 1 when we are reading keys and values */

    do
    {
        now = HttpUrlStringSearch(now, &nowlen);

        if (*now == ' ')
        {
            /* end of args */
            break;
        }
        else if (*now == '?' || *now == '&')
        {
            now += 1; // skip arg sepprator
        }
        else
        {
            if (kv == 0)
            {
                /* this is a key */
                int namelen;
                char *namestart = HttpUrlStringSearch(now, &namelen);
                dst[dstidx].name = (Slice){.buf = namestart, .len = namelen};
                kv = 1;
                now += namelen + 1; /* + 1 to skip the '='*/
            }
            else if (kv == 1)
            {
                /* this is a value */
                int vallen;
                char *valstart = HttpUrlStringSearch(now, &vallen);
                dst[dstidx].value = (Slice){.buf = valstart, .len = vallen};
                kv = 0;
                now += vallen;
                dstidx += 1; /* we got a kv, time to read another! */

                if (dstidx > dstlen)
                {
                    puts("Client sent more then dstlen args!");
                    return -1;
                }
            }
        }
    } while (*now != ' ');

    // puts(buffer);
    return dstidx;
}

void Route(
    int fd,
    Slice request,
    Slice method,
    Slice route,
    Slice body,
    int url_argc,
    SlicePair url_args[url_argc]);

void handle_request(int fd)
{
    char buffer[1024] = {0};
    SlicePair args[128] = {0};

    // i think the first word is always the method, and then the route upto the next space?
    int r = read(fd, buffer, sizeof(buffer));
    if (0 > r)
    {
        perror("read");
        exit(EXIT_FAILURE);
    }

    buffer[r] = '\0';

    // puts(buffer);

    int methodlen;
    int routelen;

    /* thease pointers are slices of buff, and there ends are set by methodlen and route len */
    char *method = HttpUrlStringSearch(buffer, &methodlen);
    char *route = HttpUrlStringSearch(buffer + methodlen + 1, &routelen);
    int used = methodlen + 1 + routelen;

    int argc = HttpUrlGetArgs(buffer + used, r - used, args, sizeof(args) / sizeof(args[0]));

    int bodylen;
    char *body = HttpGetBody(buffer, r, &bodylen);

    if (0 > argc)
    {
        close(fd);
        return;
    }

    if (!method || !route)
    {
        close(fd);
        return;
    }

    Route(fd,
          (Slice){.buf = buffer, .len = sizeof(buffer)},
          (Slice){.buf = method, .len = methodlen},
          (Slice){.buf = route, .len = routelen},
          (Slice){.buf = body, .len = bodylen},
          argc, args);
}

#define HTTP_200_OK "HTTP/1.1 200 OK\r\n"
#define HTTP_404_ERR "HTTP/1.1 404 PAGE NOT FOUND\r\n"
#define CONTENT_TEXT_HTML "Content-Type: text/html\r\n"
#define CONTENT_TEXT_JSON "Content-Type: application/json\r\n"

int Write404(int fd)
{
#define error_404_html                   \
    HTTP_404_ERR CONTENT_TEXT_HTML       \
        "\r\n<!DOCTYPE html>\r\n"        \
        "<html>\r\n"                     \
        "<head></head>\r\n"              \
        "<body>\r\n"                     \
        "That Page dose not exist!!\r\n" \
        "</body>\r\n"                    \
        "</html>\r\n\r\n"

    return WriteBuffer(fd, error_404_html, sizeof(error_404_html) / sizeof(error_404_html[0]));
#undef error_404_html
}

static int g_fiddle_serverfd;

static socklen_t addr_len = sizeof(struct sockaddr_in), caddr_len = sizeof(struct sockaddr_in);
static struct sockaddr_in g_svr, g_cli;

#define MAX_FIDDLE_VARS (10)

static struct fiddle_things
{
    struct fiddle_vec3
    {
        Vector3 *v, min, max;
        const char *lbl;
    } vec3_vars[MAX_FIDDLE_VARS];
    int vec3_vars_idx;

    struct fiddle_int
    {
        int *v, min, max;
        const char *lbl;
    } int_vars[MAX_FIDDLE_VARS];
    int int_vars_idx;

} g_fiddlestate;

void DoIntPatch(int kind, int idx, int val)
{
    printf("DO PATCH %d %d %d\n", kind, idx, val);

    if (kind != 1)
        return; // this was for floats!

    if (idx > g_fiddlestate.int_vars_idx)
        return; // idx out of range of valid places

    *g_fiddlestate.int_vars[idx].v = val;
}

// ================================ JSON BUILDER LIB
void JsonBuildArray_Start(char *dst) { dst[strlen(dst)] = '['; }
void JsonBuildArray_End(char *dst) { dst[strlen(dst)] = ']'; }
void JsonBuildArray_AddObject(char *dst, int isLast)
{
    if (!isLast)
        dst[strlen(dst)] = ',';
}

void JsonBuildObject_Start(char *dst) { dst[strlen(dst)] = '{'; }
void JsonBuildObject_End(char *dst) { dst[strlen(dst)] = '}'; }
void JsonBuildObject_AddPropertyRaw(char *dst, char *name, char *val, int isLast)
{
    dst[strlen(dst)] = '"';
    int inspos = strlen(dst);
    memcpy(dst + inspos, name, strlen(name));
    dst[strlen(dst)] = '"';
    dst[strlen(dst)] = ':';
    inspos = strlen(dst);
    memcpy(dst + inspos, val, strlen(val));
    if (!isLast)
        dst[strlen(dst)] = ',';
}
// wraps val in quotes
void JsonBuildObject_AddProperty(char *dst, char *name, char *val, int isLast)
{
    dst[strlen(dst)] = '"';
    int inspos = strlen(dst);
    memcpy(dst + inspos, name, strlen(name));
    dst[strlen(dst)] = '"';
    dst[strlen(dst)] = ':';
    dst[strlen(dst)] = '"';
    inspos = strlen(dst);
    memcpy(dst + inspos, val, strlen(val));
    dst[strlen(dst)] = '"';

    if (!isLast)
        dst[strlen(dst)] = ',';
}
// =============================== END JSON LIB

void GetVars(int fd)
{
    /*
    [

    // int kind
    {"k":1,"l":"lbl","s":0.0,"b":1.0,"n":.5, "i":0}

    // v3 kind
    {"k":0,"l":"lbl","sx":0.0, "sy":0.0, "sz":0.0, "bx":0.0 "by":0.0 "bz":0.0, "nx":0.0 "ny":0.0 "nz":0.0,  }

    ]

    "k" 0 = vec3, 1 = int
    "l" label of var
    "s" min - small
    "b" max - big
    "n" now
    "i" idx of fiddle var in kindarray vec3[i] || int[i]
    */
    char buf[1024 * 1024 * 3] = {0};

    memcpy(buf, HTTP_200_OK, sizeof(HTTP_200_OK));
    memcpy(strlen(buf) + buf, CONTENT_TEXT_JSON, sizeof(CONTENT_TEXT_JSON));
    buf[strlen(buf)] = '\r';
    buf[strlen(buf)] = '\n';

    JsonBuildArray_Start(buf);

    char tostrbuf[20];
    memset(tostrbuf, 0, sizeof(tostrbuf));

    for (size_t i = 0; i < g_fiddlestate.int_vars_idx; i++)
    {
        struct fiddle_int fi = g_fiddlestate.int_vars[i];

        JsonBuildObject_Start(buf);
        {
            JsonBuildObject_AddPropertyRaw(buf, "k", "1", 0);
            JsonBuildObject_AddProperty(buf, "l", fi.lbl, 0);

            memset(tostrbuf, 0, sizeof(tostrbuf));
            sprintf(tostrbuf, "%d", fi.min);
            JsonBuildObject_AddProperty(buf, "s", tostrbuf, 0);

            memset(tostrbuf, 0, sizeof(tostrbuf));
            sprintf(tostrbuf, "%d", fi.max);
            JsonBuildObject_AddProperty(buf, "b", tostrbuf, 0);

            memset(tostrbuf, 0, sizeof(tostrbuf));
            sprintf(tostrbuf, "%d", *fi.v);
            JsonBuildObject_AddProperty(buf, "n", tostrbuf, 0);

            memset(tostrbuf, 0, sizeof(tostrbuf));
            sprintf(tostrbuf, "%ld", i);
            JsonBuildObject_AddProperty(buf, "i", tostrbuf, 1);
        }
        JsonBuildObject_End(buf);

        int last_for_all = (i == g_fiddlestate.int_vars_idx - 1) && (!g_fiddlestate.vec3_vars_idx);

        JsonBuildArray_AddObject(buf, last_for_all);
    }

    for (size_t i = 0; i < g_fiddlestate.vec3_vars_idx; i++)
    {
        struct fiddle_vec3 fv = g_fiddlestate.vec3_vars[i];

        JsonBuildObject_Start(buf);
        {
            JsonBuildObject_AddPropertyRaw(buf, "k", "0", 0);
            JsonBuildObject_AddProperty(buf, "l", fv.lbl, 0);
            
            { // small
                memset(tostrbuf, 0, sizeof(tostrbuf));
                sprintf(tostrbuf, "%f", fv.min.x);
                JsonBuildObject_AddProperty(buf, "sx", tostrbuf, 0);

                memset(tostrbuf, 0, sizeof(tostrbuf));
                sprintf(tostrbuf, "%f", fv.min.y);
                JsonBuildObject_AddProperty(buf, "sy", tostrbuf, 0);

                memset(tostrbuf, 0, sizeof(tostrbuf));
                sprintf(tostrbuf, "%f", fv.min.z);
                JsonBuildObject_AddProperty(buf, "sz", tostrbuf, 0);
            }

            { // big
                memset(tostrbuf, 0, sizeof(tostrbuf));
                sprintf(tostrbuf, "%f", fv.max.x);
                JsonBuildObject_AddProperty(buf, "bx", tostrbuf, 0);

                memset(tostrbuf, 0, sizeof(tostrbuf));
                sprintf(tostrbuf, "%f", fv.max.y);
                JsonBuildObject_AddProperty(buf, "by", tostrbuf, 0);

                memset(tostrbuf, 0, sizeof(tostrbuf));
                sprintf(tostrbuf, "%f", fv.max.z);
                JsonBuildObject_AddProperty(buf, "bz", tostrbuf, 0);
            }

            { // now
                memset(tostrbuf, 0, sizeof(tostrbuf));
                sprintf(tostrbuf, "%f", fv.v->x);
                JsonBuildObject_AddProperty(buf, "nx", tostrbuf, 0);

                memset(tostrbuf, 0, sizeof(tostrbuf));
                sprintf(tostrbuf, "%f", fv.v->y);
                JsonBuildObject_AddProperty(buf, "ny", tostrbuf, 0);

                memset(tostrbuf, 0, sizeof(tostrbuf));
                sprintf(tostrbuf, "%f", fv.v->z);
                JsonBuildObject_AddProperty(buf, "nz", tostrbuf, 0);
            }

            memset(tostrbuf, 0, sizeof(tostrbuf));
            sprintf(tostrbuf, "%ld", i);
            JsonBuildObject_AddProperty(buf, "i", tostrbuf, 1);
        }
        JsonBuildObject_End(buf);

        JsonBuildArray_AddObject(buf, i == g_fiddlestate.vec3_vars_idx - 1);
    }

    JsonBuildArray_End(buf);

    // printf("\'%s\'\n", buf);


    WriteBuffer(fd, buf, strlen(buf));
}

#define JSON_IMPL
#include "json.h"

void PatchVec3(int fd, Slice body)
{

}

void PatchInt(int fd, Slice body)
{
    char *kind = NULL;
    int kind_len = 0;

    char *idx = NULL;
    int idx_len = 0;

    char *val = NULL;
    int val_len = 0;

    ParseJson(body.buf, body.len, "k", &kind, &kind_len);
    ParseJson(body.buf, body.len, "i", &idx, &idx_len);
    ParseJson(body.buf, body.len, "v", &val, &val_len);

    if (!kind || !idx || !val)
    {
        printf("PatchVars: Invalid json\n");
        goto DONE;
    }

    int i_kind = -1, i_idx = -1, i_val = -1;

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));

    if (kind_len > sizeof(buffer))
    {
        puts("kind too long");
        goto DONE;
    }
    if (idx_len > sizeof(buffer))
    {
        puts("idx too long");
        goto DONE;
    }
    if (val_len > sizeof(buffer))
    {
        puts("val too long");
        goto DONE;
    }

    memcpy(buffer, kind, kind_len);
    i_kind = atoi(buffer);
    memset(buffer, 0, sizeof(buffer));

    memcpy(buffer, idx, idx_len);
    i_idx = atoi(buffer);
    memset(buffer, 0, sizeof(buffer));

    memcpy(buffer, val, val_len);
    i_val = atoi(buffer);
    memset(buffer, 0, sizeof(buffer));

    DoIntPatch(i_kind, i_idx, i_val);

DONE:

    char buf[sizeof(HTTP_200_OK)] = {0};
    memcpy(buf, HTTP_200_OK, sizeof(HTTP_200_OK));
    WriteBuffer(fd, buf, strlen(buf));
}

void JsonTest()
{
    // JsonBuildArray_Start(buffer);
    // JsonBuildObject_Start(buffer);
    // JsonBuildObject_AddProperty(buffer, "name", "\"val\"", 1);
    // JsonBuildObject_End(buffer);
    // JsonBuildArray_AddObject(buffer, 0);
    // JsonBuildObject_Start(buffer);
    // JsonBuildObject_End(buffer);
    // JsonBuildArray_AddObject(buffer, 1);
    // JsonBuildArray_End(buffer);
    GetVars(-1);
}

#define ROUTE_START \
    if (0)          \
    {               \
    }

#define ROUTE(METHOD, ROUTE) else if (slice_cmp(SLICE(METHOD), method) == 0 && slice_cmp(SLICE(ROUTE), route) == 0)

#define ROUTE_END     \
    else              \
    {                 \
        Write404(fd); \
    }

#include <fcntl.h>
#include <sys/stat.h>
typedef struct file_memmap
{
    int len, fd;
    char *map;
} file_memmap;
void CloseFileMemMap(file_memmap *fc)
{
    munmap(fc, fc->len);
    close(fc->fd);

    fc->map = NULL;
    fc->len = 0;
    fc->fd = 0;
}

// -1 on error 0 on ok
int OpenFileMemMap(file_memmap *fc, const char *fpath)
{
    int fd = open(fpath, O_RDONLY);

    if (fd == -1)
    {
        perror("open");
        return -1;
    }

    struct stat sb;

    if (fstat(fd, &sb) == -1)
    {
        perror("fstat");
        return -1;
    }

    void *map = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (map == MAP_FAILED)
    {
        close(fd);
        return -1;
    }

    fc->map = (char *)map;
    fc->len = sb.st_size;
    fc->fd = fd;

    return 0;
}

#define HTML_HEADER HTTP_200_OK CONTENT_TEXT_HTML "\r\n"
const int html_header_len = sizeof(HTML_HEADER) / sizeof(HTML_HEADER[0]);

int AppendHTMLHeaderAndWriteBuffer(int fd, char *buffer, int bufferlen)
{

    char *m = calloc(bufferlen + html_header_len, sizeof(char));

    if (!m)
        return -1;

    memcpy(m, HTML_HEADER, html_header_len);
    memcpy(m + html_header_len, buffer, bufferlen);

    int r = WriteBuffer(fd, m, bufferlen + html_header_len);

    free(m);

    return r;
}

int WriteFileDirectly(int fd, const char *fpath)
{
    file_memmap f;
    if (0 > OpenFileMemMap(&f, fpath))
    {
        perror("mmap");
        return -1;
    }
    int ret = AppendHTMLHeaderAndWriteBuffer(fd, f.map, f.len);
    CloseFileMemMap(&f);
    return ret;
}

void Route(
    int fd,
    Slice request, Slice method, Slice route, Slice body,
    int url_argc,
    SlicePair url_args[url_argc])
{
    // printf("Method:" SLICE_FMT "\nRoute:" SLICE_FMT "\n", SLICE_PNT(method), SLICE_PNT(route));

    ROUTE_START
    ROUTE("GET", "/")
    {
        WriteFileDirectly(fd, "www/index.html");
    }
    ROUTE("GET", "/test")
    {
        WriteFileDirectly(fd, "www/test.html");
    }
    ROUTE("GET", "/vars")
    {
        GetVars(fd);
    }
    ROUTE("PATCH", "/int")
    {
        PatchInt(fd, body);
    }
    ROUTE("PATCH", "/vec3")
    {
        PatchVec3(fd, body);
    }
    ROUTE_END;
}

void CheckForNewRequests()
{
    int cfd = accept(g_fiddle_serverfd, (struct sockaddr *)&g_svr, &caddr_len);
    if (0 > cfd && (errno == EWOULDBLOCK || errno == EAGAIN))
    {
        return;
    }
    else if (0 > cfd)
    {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    // we have a new client
    // LOG("Accepted new client!");

    handle_request(cfd);
    close(cfd);
}

void Wf_StartServer(int port)
{
    LOG("Starting Server");

    g_fiddle_serverfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

    if (0 > g_fiddle_serverfd)
    {
        perror("SOCKET");
        exit(EXIT_FAILURE);
    }

    if (0 > setsockopt(g_fiddle_serverfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    g_svr.sin_family = AF_INET;
    g_svr.sin_addr.s_addr = INADDR_ANY;
    g_svr.sin_port = htons(port);

    if (0 > bind(g_fiddle_serverfd, (struct sockaddr *)&g_svr, addr_len))
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (0 > listen(g_fiddle_serverfd, 1))
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }
}

void Wf_RunFiddle()
{
    CheckForNewRequests();
}

void Wf_FiddleWithVec3(Vector3 *v, const char *lbl, Vector3 min, Vector3 max)
{
    g_fiddlestate.vec3_vars[g_fiddlestate.vec3_vars_idx].lbl = lbl;
    g_fiddlestate.vec3_vars[g_fiddlestate.vec3_vars_idx].min = min;
    g_fiddlestate.vec3_vars[g_fiddlestate.vec3_vars_idx].max = max;
    g_fiddlestate.vec3_vars[g_fiddlestate.vec3_vars_idx].v = v;
    g_fiddlestate.vec3_vars_idx += 1;
}

void Wf_FiddleWithInt(int *v, const char *lbl, int min, int max)
{
    g_fiddlestate.int_vars[g_fiddlestate.int_vars_idx].lbl = lbl;
    g_fiddlestate.int_vars[g_fiddlestate.int_vars_idx].min = min;
    g_fiddlestate.int_vars[g_fiddlestate.int_vars_idx].max = max;
    g_fiddlestate.int_vars[g_fiddlestate.int_vars_idx].v = v;
    g_fiddlestate.int_vars_idx += 1;
}
