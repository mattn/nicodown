//#define CURL_STATICLIB
#include <stdlib.h>
#include <memory.h>
#include <curl/curl.h>
#ifdef USE_LIBXML
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#endif

#define HEX_DIGITS "0123456789ABCDEF"
#define IS_QUOTED(x) (*x == '%' && strchr(HEX_DIGITS, *(x+1)) && strchr(HEX_DIGITS, *(x+2)))

#define WATCH_URL_BASE "http://www.nicovideo.jp/watch/"

typedef struct {
    char* data;     // response data from server
    size_t size;    // response size of data
} MEMFILE;

MEMFILE*
memfopen() {
    MEMFILE* mf = (MEMFILE*) malloc(sizeof(MEMFILE));
    mf->data = NULL;
    mf->size = 0;
    return mf;
}

void
memfclose(MEMFILE* mf) {
    if (mf->data) free(mf->data);
    free(mf);
}

size_t
memfwrite(char* ptr, size_t size, size_t nmemb, void* stream) {
    MEMFILE* mf = (MEMFILE*) stream;
    int block = size * nmemb;
    if (!mf->data)
        mf->data = (char*) malloc(block);
    else
        mf->data = (char*) realloc(mf->data, mf->size + block);
    if (mf->data) {
        memcpy(mf->data + mf->size, ptr, block);
        mf->size += block;
    }
    return block;
}

char*
memfstrdup(MEMFILE* mf) {
    char* buf = malloc(mf->size + 1);
    memcpy(buf, mf->data, mf->size);
    buf[mf->size] = 0;
    return buf;
}

int
progress(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow) {
    printf("\r[%s] %d%%   ", (char*) clientp, 100 * (int) (dlnow / dltotal));
    fflush(stdout);
    return 0;
}

int
main(int argc, char* argv[]) {
    CURL* curl = NULL;
    CURLcode res;
    int status = 0;
    char error[CURL_ERROR_SIZE];
    char fname[256];
    char line[256];
    char cookie[256];
    char query[2048];
    char* buf = NULL;
    char* ptr = NULL;
    char* tmp = NULL;
    char* id;
    FILE* fp = NULL;
    MEMFILE* mf; // mem file
    MEMFILE* hf; // mem file for header

    // usage
    if (argc != 2) {
        fputs("usage: nicodown [video_id]\n", stderr);
        goto leave;
    }

    if (!(ptr = getenv("HOME"))) {
#ifdef _WIN32
        if (!(ptr = getenv("USERPROFILE"))) {
            fprintf(stderr, "failed to get HOME directory\n");
            goto leave;
        }
#else
        fprintf(stderr, "failed to get HOME directory\n");
        goto leave;
#endif
    }

    sprintf(fname, "%s/.nicodownrc", ptr);
    fp = fopen(fname, "r");
    if (!fp || !fgets(line, sizeof(line), fp)) {
        if (fp) fclose(fp);
        fprintf(stderr, "failed to read ~/.nicodownrc\n");
        goto leave;
    }
    fclose(fp);
    tmp = strpbrk(line, "\r\n");
    if (tmp) *tmp = 0;
    ptr = strchr(line, ':');
    if (!ptr) {
        fprintf(stderr, "failed to parse ~/.nicodownrc\n");
        goto leave;
    }
    *ptr++ = 0;

    id = argv[1];
    if (!strncmp(id, WATCH_URL_BASE, strlen(WATCH_URL_BASE))) {
        id += strlen(WATCH_URL_BASE);
    }
    sprintf(query, "mail_tel=%s&password=%s&next_url=/watch/%s", line, ptr, id);

    // default filename
    sprintf(fname, "%s.flv", id);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, memfwrite);
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

    // login
    mf = memfopen();
    hf = memfopen();
    curl_easy_setopt(curl, CURLOPT_URL, "https://secure.nicovideo.jp/secure/login?site=niconico");
    curl_easy_setopt(curl, CURLOPT_POST, 1);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, query);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, memfwrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, mf);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, hf);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fputs(error, stderr);
        memfclose(mf);
        memfclose(hf);
        goto leave;
    }

    // parse cookie
    memset(cookie, 0, sizeof(cookie));
    buf = memfstrdup(hf);
    tmp = buf;
    while (tmp = strstr(tmp, "Set-Cookie: ")) {
        ptr = tmp;
        tmp = strpbrk(ptr, "\r\n;");
        if (tmp) *tmp = 0;
        strcpy(cookie, ptr + 12);
        if (strncmp(cookie, "user_session=user", 17) == 0) {
            curl_easy_setopt(curl, CURLOPT_COOKIE, cookie);
            break;
        }
        tmp++;
    }
    free(buf);

    memfclose(mf);
    memfclose(hf);
    mf = memfopen();
    hf = memfopen();
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, mf);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, hf);

    // redirect
    sprintf(query, "http://www.nicovideo.jp/watch/%s", id);
    curl_easy_setopt(curl, CURLOPT_URL, query);
    curl_easy_setopt(curl, CURLOPT_POST, 0);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fputs(error, stderr);
        memfclose(mf);
        memfclose(hf);
        goto leave;
    }

    // parse cookie
    buf = memfstrdup(hf);
    ptr = NULL;
    tmp = buf;
    while (tmp = strstr(tmp, "Set-Cookie: "))
        ptr = tmp++;
    if (ptr) {
        tmp = strpbrk(ptr, "\r\n;");
        if (tmp) *tmp = 0;
        strcat(cookie, "; ");
        strcat(cookie, ptr + 12);
        curl_easy_setopt(curl, CURLOPT_COOKIE, cookie);
    }
    free(buf);

    // parse response body
    buf = memfstrdup(mf);
    if (buf && strstr(buf, "id=\"login_bar\"")) {
        free(buf);
        fprintf(stderr, "failed to login\n");
        memfclose(mf);
        memfclose(hf);
        goto leave;
    }
    free(buf);
    memfclose(hf);
    memfclose(mf);

    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, NULL);

    // get video query, and get filename
    sprintf(query, "http://www.nicovideo.jp/api/getthumbinfo?v=%s", id);
    mf = memfopen();
    curl_easy_setopt(curl, CURLOPT_URL, query);
    curl_easy_setopt(curl, CURLOPT_POST, 0);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, mf);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fputs(error, stderr);
        memfclose(mf);
        goto leave;
    }

#ifdef USE_LIBXML
    // parse title node
    {
        xmlDocPtr doc = NULL;
        xmlXPathContextPtr xpathctx;
        xmlXPathObjectPtr xpathobj;

        doc = xmlParseMemory(mf->data, mf->size);
        xpathctx = doc ? xmlXPathNewContext(doc) : NULL;
        xpathobj = xpathctx ? xmlXPathEvalExpression((xmlChar*) "//title", xpathctx) : NULL;
        if (xpathobj) {
            int n;
            xmlNodeSetPtr nodes = xpathobj->nodesetval;
            for(n = 0; nodes && n < xmlXPathNodeSetGetLength(nodes); n++) {
                xmlNodePtr node = nodes->nodeTab[n];
                if(node->type != XML_ATTRIBUTE_NODE && node->type != XML_ELEMENT_NODE && node->type != XML_CDATA_SECTION_NODE) continue;
                if (node->type == XML_CDATA_SECTION_NODE)
                    sprintf(fname, "%s.flv", (char*) node->content);
                else
                    if (node->children)
                        sprintf(fname, "%s.flv", (char*) node->children->content);
                break;
            }
        }
        if (xpathobj ) xmlXPathFreeObject(xpathobj);
        if (xpathctx) xmlXPathFreeContext(xpathctx);
        if (doc) xmlFreeDoc(doc);
    }
#else
    buf = memfstrdup(mf);
    ptr = buf ? strstr(buf, "<title>") : NULL;
    if (ptr) {
        ptr += 7;
        tmp = strstr(ptr, "</title>");
        if (*tmp) {
            *tmp = 0;
            sprintf(fname, "%s.flv", ptr);
        }
    }
    if (buf) free(buf);
#endif

    memfclose(mf);

#ifdef _WIN32
    {
        UINT codePage;
        size_t wcssize;
        wchar_t* wcsstr;
        size_t mbssize;
        char* mbsstr;

        codePage = CP_UTF8;
        wcssize = MultiByteToWideChar(codePage, 0, fname, -1,  NULL, 0);
        wcsstr = (wchar_t*) malloc(sizeof(wchar_t) * (wcssize + 1));
        wcssize = MultiByteToWideChar(codePage, 0, fname, -1, wcsstr, wcssize + 1);
        wcsstr[wcssize] = 0;
        codePage = GetACP();
        mbssize = WideCharToMultiByte(codePage, 0, (LPCWSTR) wcsstr,-1,NULL,0,NULL,NULL);
        mbsstr = (char*) malloc(mbssize+1);
        mbssize = WideCharToMultiByte(codePage, 0, (LPCWSTR) wcsstr, -1, mbsstr, mbssize, NULL, NULL);
        mbsstr[mbssize] = 0;
        strcpy(fname, mbsstr);
        free(mbsstr);
        free(wcsstr);
    }
#endif

    // get video query
    sprintf(query, "http://flapi.nicovideo.jp/api/getflv?v=%s", id);
    mf = memfopen();
    curl_easy_setopt(curl, CURLOPT_URL, query);
    curl_easy_setopt(curl, CURLOPT_POST, 0);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, mf);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fputs(error, stderr);
        memfclose(mf);
        goto leave;
    }
    buf = memfstrdup(mf);
    ptr = strstr(buf, "url=");
    if (!ptr) {
        if (buf) free(buf);
        fprintf(stderr, "failed to get video info\n");
        memfclose(mf);
        goto leave;
    }
    tmp = strstr(ptr, "&");
    if (tmp) *tmp = 0;
    tmp = ptr;
    while(*tmp) {
        if (IS_QUOTED(tmp)) {
            unsigned int num = 0;
            sscanf(tmp+1, "%02x", &num);
            *tmp = (char)num;
            strcpy(tmp + 1, tmp + 3);
        }
        tmp++;
    }
    strcpy(query, ptr + 4);
    printf("URL: %s\n", query);
    free(buf);
    memfclose(mf);

    // sanitize filename
    ptr = fname;
    while (*ptr) {
        if (strchr("\\/|:<>\"?*", *ptr)) *ptr = '_';
        ptr++;
    }

    // download video
    fp = fopen(fname, "wb");
    if (!fp) {
        fprintf(stderr, "failed to open file: %s\n", fname);
        goto leave;
    }
    curl_easy_setopt(curl, CURLOPT_URL, query);
    curl_easy_setopt(curl, CURLOPT_POST, 0);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
    curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, (void*)fname);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 2048);
    res = curl_easy_perform(curl);
    fclose(fp);
    if (res != CURLE_OK) {
        fputs(error, stderr);
        goto leave;
    }

leave:
    if (curl) curl_easy_cleanup(curl);
    printf("\n");

    return 0;
}
