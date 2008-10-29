//#define CURL_STATICLIB
#include <curl/curl.h>

#define HEX_DIGITS "0123456789ABCDEF"
#define IS_QUOTED(x) (*x == '%' && strchr(HEX_DIGITS, *(x+1)) && strchr(HEX_DIGITS, *(x+2)))


typedef struct {
	char* data;		/* response data from server. */
	size_t size;	/* response size of data */
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
		mf->data = (char*)malloc(block);
	else
		mf->data = (char*)realloc(mf->data, mf->size + block);
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
	buf[mf->size + 1] = 0;
	return buf;
}

int
main(int argc, char* argv[]) {
	CURLcode res;
	CURL* curl;
	char error[256];
	char name[256];
	char data[1024];
	char* buf = NULL;
	char* ptr = NULL;
	char* tmp = NULL;
	FILE* fp = NULL;
	int status = 0;
	MEMFILE* mf;

	// usage
	if (argc != 4) {
		fputs("usage: nicodown [usermail] [password] [video_id]", stderr);
		goto leave;
	}

	// default filename
	sprintf(name, "%s.flv", argv[3]);

	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, &error);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, memfwrite);
	curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "cookies.jar");
	curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "cookies.txt");

	// login
	sprintf(data, "mail=%s&password=%s&next_url=/watch/%s", argv[1], argv[2], argv[3]);
	mf = memfopen();
	curl_easy_setopt(curl, CURLOPT_URL, "https://secure.nicovideo.jp/secure/login?site=niconico");
	curl_easy_setopt(curl, CURLOPT_POST, 1);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, mf);
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr, error);
		memfclose(mf);
		goto leave;
	}
	buf = memfstrdup(mf);
	if (strstr(buf, "id=\"login_bar\"")) {
		printf("%s\n", buf);
		free(buf);
		fprintf(stderr, "failed to login\n");
		goto leave;
	}
	free(buf);
	memfclose(mf);

	// get video url, and get filename
	sprintf(data, "http://www.nicovideo.jp/api/getthumbinfo?v=%s", argv[3]);
	mf = memfopen();
	curl_easy_setopt(curl, CURLOPT_URL, data);
	curl_easy_setopt(curl, CURLOPT_POST, 0);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, mf);
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr, error);
		memfclose(mf);
		goto leave;
	}
	buf = memfstrdup(mf);
	ptr = buf ? strstr(buf, "<title>") : NULL;
	if (ptr) {
		ptr += 7;
		tmp = strstr(ptr, "</title>");
		if (*tmp) {
			*tmp = 0;
			strcpy(name, ptr);
		}
#ifdef _WIN32
		{
			UINT codePage;
			size_t wcssize;
			wchar_t* wcsstr;
			size_t mbssize;
			char* mbsstr;

			codePage = CP_UTF8;
			wcssize = MultiByteToWideChar(codePage, 0, name, -1,  NULL, 0);
			wcsstr = (wchar_t*)malloc(sizeof(wchar_t) * (wcssize + 1));
			wcssize = MultiByteToWideChar(codePage, 0, ptr, -1, wcsstr, wcssize + 1);
			wcsstr[wcssize] = 0;
			codePage = GetACP();
			mbssize = WideCharToMultiByte(codePage, 0, (LPCWSTR)wcsstr,-1,NULL,0,NULL,NULL);
			mbsstr = (char*)malloc(mbssize+1);
			mbssize = WideCharToMultiByte(codePage, 0, (LPCWSTR)wcsstr, -1, mbsstr, mbssize, NULL, NULL);
			mbsstr[mbssize] = 0;
			sprintf(name, "%s.flv", mbsstr);
			free(mbsstr);
			free(wcsstr);
		}
#endif
	}
	if (buf) free(buf);
	printf("downloading %s\n", name);
	memfclose(mf);

	// get video url
	sprintf(data, "http://www.nicovideo.jp/api/getflv?v=%s", argv[3]);
	mf = memfopen();
	curl_easy_setopt(curl, CURLOPT_URL, data);
	curl_easy_setopt(curl, CURLOPT_POST, 0);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, mf);
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr, error);
		memfclose(mf);
		goto leave;
	}
	buf = memfstrdup(mf);
	ptr = buf ? strstr(buf, "url=") : NULL;
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
			char num = 0;
			sscanf(tmp+1, "%02x", &num);
			*tmp = num;
			strcpy(tmp + 1, tmp + 3);
		}
		tmp++;
	}
	strcpy(data, ptr + 4);
	printf("URL: %s\n", data);
	free(buf);
	memfclose(mf);

	// download video
	fp = fopen(name, "wb");
	if (!fp) {
		fprintf(stderr, "failed to open file\n");
		goto leave;
	}
	curl_easy_setopt(curl, CURLOPT_URL, data);
	curl_easy_setopt(curl, CURLOPT_POST, 0);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	res = curl_easy_perform(curl);
	fclose(fp);
	if (res != CURLE_OK) {
		fprintf(stderr, error);
		goto leave;
	}

leave:
	curl_easy_cleanup(curl);

	return 0;
}
