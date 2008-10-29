//#define CURL_STATICLIB
#include <curl/curl.h>

#define HEX_DIGITS "0123456789ABCDEF"
#define IS_QUOTED(x) (*x == '%' && strchr(HEX_DIGITS, *(x+1)) && strchr(HEX_DIGITS, *(x+2)))

static char* response_data = NULL;	/* response data from server. */
static size_t response_size = 0;	/* response size of data */

static void
curl_handle_init() {
	response_data = NULL;
	response_size = 0;
}

static void
curl_handle_term() {
	if (response_data) free(response_data);
}

static size_t
curl_handle_returned_data(char* ptr, size_t size, size_t nmemb, void* stream) {
	if (!response_data)
		response_data = (char*)malloc(size*nmemb);
	else
		response_data = (char*)realloc(response_data, response_size+size*nmemb);
	if (response_data) {
		memcpy(response_data+response_size, ptr, size*nmemb);
		response_size += size*nmemb;
	}
	return size*nmemb;
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
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_handle_returned_data);
	curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "cookies.jar");
	curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "cookies.txt");

	// login
	sprintf(data, "mail=%s&password=%s&next_url=/watch/%s", argv[1], argv[2], argv[3]);
	curl_handle_init();
	curl_easy_setopt(curl, CURLOPT_URL, "https://secure.nicovideo.jp/secure/login?site=niconico");
	curl_easy_setopt(curl, CURLOPT_POST, 1);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr, error);
		goto leave;
	}
	buf = malloc(response_size + 1);
	strcpy(buf, response_data);
	if (strstr(buf, "id=\"login_bar\"")) {
		printf("%s\n", buf);
		free(buf);
		fprintf(stderr, "failed to login\n");
		goto leave;
	}
	free(buf);
	curl_handle_term();

	// get video url, and get filename
	sprintf(data, "http://www.nicovideo.jp/api/getthumbinfo?v=%s", argv[3]);
	curl_handle_init();
	curl_easy_setopt(curl, CURLOPT_URL, data);
	curl_easy_setopt(curl, CURLOPT_POST, 0);
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr, error);
		goto leave;
	}
	buf = malloc(response_size + 1);
	strcpy(buf, response_data);
	ptr = strstr(buf, "<title>");
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
	free(buf);
	printf("downloading %s\n", name);
	curl_handle_term();

	// get video url
	sprintf(data, "http://www.nicovideo.jp/api/getflv?v=%s", argv[3]);
	curl_handle_init();
	curl_easy_setopt(curl, CURLOPT_URL, data);
	curl_easy_setopt(curl, CURLOPT_POST, 0);
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr, error);
		goto leave;
	}
	buf = malloc(response_size + 1);
	strcpy(buf, response_data);
	ptr = strstr(response_data, "url=");
	if (!ptr) {
		free(buf);
		fprintf(stderr, "failed to get video info\n");
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
	curl_handle_term();

	// download video
	fp = fopen(name, "wb");
	if (!fp) {
		fprintf(stderr, "failed to open file\n");
		goto leave;
	}
	curl_handle_init();
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
	curl_handle_term();
	curl_easy_cleanup(curl);

	return 0;
}
