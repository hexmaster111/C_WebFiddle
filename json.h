#ifndef JSON_C
#define JSON_C

void ParseJson(
    char *in_start,           // string to parse the json outta
    int in_len,               // total len of json message
    const char *in_fieldname, // filed to search for
    char **out_fieldvalue,    // the start pointer
    int *out_fieldvaluelen    // len of field parsed
);

#endif // JSON_C

#ifdef JSON_IMPL
#undef JSON_IMPL

struct JsonParseContext
{
    char *now;
    int remain;
};
static char Now(struct JsonParseContext ctx) { return *ctx.now; }
static void Skip(struct JsonParseContext *ctx)
{
    ctx->now += 1;
    ctx->remain -= 1;
}

static int AtEndOfInput(struct JsonParseContext ctx) { return 0 > ctx.remain; }

/* reads up to first quote it finds */
static void SkipToFieldStart(struct JsonParseContext *ctx)
{
    char n = Now(*ctx);
    while (n != '"')
    {
        Skip(ctx);
        n = Now(*ctx);
    }
    Skip(ctx); // skip the "
}

/*  we should be at a field start, just after the "  */
static int IsFieldWeCareAbout(struct JsonParseContext *ctx, const char *care_about)
{

    int care_about_len = strlen(care_about);

    int field_now_len = 0;

    // todo count len of field in json, if len same, strcmp , else return falsse
    char *c = ctx->now;
    for (field_now_len = 0; field_now_len < ctx->remain; field_now_len++)
    {
        if (*c == '"')
            goto GotLen;
        else
            c += 1;
    }

    return 0; /* no, we dont care about this, it was some invalid json */

GotLen:
    int ret = 0;

    if (field_now_len == care_about_len)
    {
        ret = strncmp(ctx->now, care_about, care_about_len) == 0;
    }

    for (int i = 0; i < field_now_len; i++)
    {
        Skip(ctx); /* consume the field name regardless */
    }

    Skip(ctx); /* consume the closing quote regardless */

    return ret;
}

/* saves everything up to the ending " OR the end of content */
static void SaveFieldValue(struct JsonParseContext *ctx, char **out_value, int *out_len)
{
    *out_value = ctx->now;

    int excape = 0;

    for (*out_len = 0; *out_len < ctx->remain; *out_len += 1)
    {
        char c = *(ctx->now + (*out_len));
        char cnext = *(ctx->now + 1 + (*out_len));

        if (c == '"' && !excape) break;
        else if (c == '\\' && !excape) excape = 1;
        else if(c == '"' && excape) excape = 0;
        else if(c == '\\' && excape) excape = 0;

    }

    // even if we dont break, we are still just saving the max len of the field value
}

static void DiscardFieldValue(struct JsonParseContext *ctx) {}

void ParseJson(
    // in slice {
    // string to parse the json outta
    char *in_start,
    // total len of json message
    int in_len,
    // }

    // the property name we are intersted in
    const char *in_fieldname,

    // out slice {
    // place to save ptr to first char in in_start
    char **out_fieldvalue,
    // len of field parsed
    int *out_fieldvaluelen
    // }
)
{
    //  {"from":"username","message":"Hello world! welcom to the json!"}
    struct JsonParseContext pctx = {.now = in_start, .remain = in_len};

    while (!AtEndOfInput(pctx))
    {
        SkipToFieldStart(&pctx); // should be the first time we hit a quote
        // "from":"username","message":"Hello world! welcom to the json!"}

        /* bad json error handling (unexpected end of input) */
        if (AtEndOfInput(pctx))
            break;

        if (IsFieldWeCareAbout(&pctx, in_fieldname)) // if the value in the quote matches the field name (and len)
        {
            SkipToFieldStart(&pctx);
            /* username","message":"Hello world! welcom to the json!"}*/

            SaveFieldValue(&pctx, out_fieldvalue, out_fieldvaluelen);
            /*/ "message":"Hello world! welcom to the json!"}*/

            return;
        }
        else
        {
            DiscardFieldValue(&pctx);
            // "message":"Hello world! welcom to the json!"}
        }
    }

    /* end of input, field not found */

    *out_fieldvalue = NULL;
    *out_fieldvaluelen = 0;

    return;
}

#endif