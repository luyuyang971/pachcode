#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winhttp.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <regex>
#include <filesystem>
#include <thread>
#include <chrono>
#include <utility>
#include <conio.h>

#pragma comment(lib, "winhttp.lib")

using namespace std;

// 解析URL
void parse_url(const string& url, wstring& host, wstring& path, INTERNET_SCHEME& scheme) {
    URL_COMPONENTS comp = { sizeof(comp) };
    wchar_t urlw[2048];
    mbstowcs(urlw, url.c_str(), sizeof(urlw) / sizeof(wchar_t));
    wchar_t hostw[256]{}, pathw[1024]{};
    comp.lpszHostName = hostw; comp.dwHostNameLength = sizeof(hostw) / sizeof(wchar_t) - 1;
    comp.lpszUrlPath = pathw;  comp.dwUrlPathLength = sizeof(pathw) / sizeof(wchar_t) - 1;
    WinHttpCrackUrl(urlw, 0, 0, &comp);
    host = hostw;
    path = pathw[0] ? pathw : L"/";
    scheme = comp.nScheme;
}

// 判断Content-Type是否为文件型
bool is_content_type_file(HINTERNET hRequest) {
    DWORD dwSize = 0;
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &dwSize, WINHTTP_NO_HEADER_INDEX);
    if (dwSize == 0)
        return false;
    vector<wchar_t> buf(dwSize / sizeof(wchar_t) + 1);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, buf.data(), &dwSize, WINHTTP_NO_HEADER_INDEX);
    wstring ct(buf.data());
    return ct.find(L"image/") != wstring::npos ||
        ct.find(L"audio/") != wstring::npos ||
        ct.find(L"video/") != wstring::npos ||
        ct.find(L"application/octet-stream") != wstring::npos;
}

// 获取Content-Type扩展名
string ext_from_content_type(const wstring& ct) {
    //jpeg、png、gif、bmp、webp、svg、mp4、mp3、wav、ogg、pdf、zip、rar、txt、doc、docx、ppt、pptx
    if (ct.find(L"jpeg") != wstring::npos)
        return ".jpg";
    if (ct.find(L"png") != wstring::npos)
        return ".png";
    if (ct.find(L"gif") != wstring::npos)
        return ".gif";
    if (ct.find(L"bmp") != wstring::npos)
        return ".bmp";
    if (ct.find(L"webp") != wstring::npos)
        return ".webp";
    if (ct.find(L"svg") != wstring::npos)
        return ".svg";
    if (ct.find(L"mp4") != wstring::npos)
        return ".mp4";
    if (ct.find(L"mp3") != wstring::npos)
        return ".mp3";
	if (ct.find(L"wav") != wstring::npos)
		return ".wav";
    if (ct.find(L"ogg") != wstring::npos)
		return ".ogg";
    if (ct.find(L"pdf") != wstring::npos)
		return ".pdf";
    if (ct.find(L"zip") != wstring::npos)
		return ".zip";
	if (ct.find(L"rar") != wstring::npos)
		return ".rar";
    if (ct.find(L"txt") != wstring::npos)
        return ".txt";
    if (ct.find(L"doc") != wstring::npos)
        return ".doc";
    if (ct.find(L"docx") != wstring::npos)
        return ".docx";
    if (ct.find(L"ppt") != wstring::npos)
        return ".ppt";
    if (ct.find(L"pptx") != wstring::npos)
        return ".pptx";
    return ".file";
}

// WinHTTP GET，返回内容和类型判断和Content-Type
struct HttpResult {
    string data;
    bool is_file;
    wstring content_type;
};
HttpResult winhttp_get_and_type(const wstring& host, const wstring& path, INTERNET_SCHEME scheme) {
    HINTERNET hSession = WinHttpOpen(L"Crawler/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession)
        return { "", false, L"" };
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), scheme == INTERNET_SCHEME_HTTPS ? 443 : 80, 0);
    if (!hConnect) { 
        WinHttpCloseHandle(hSession); 
        return { "", false, L"" }; 
    }
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        scheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) { 
        WinHttpCloseHandle(hConnect); 
        WinHttpCloseHandle(hSession); 
        return { "", false, L"" }; 
    }
    BOOL bResult = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        && WinHttpReceiveResponse(hRequest, NULL);
    string result;
    bool is_file = false;
    wstring ct;
    if (bResult) {
        DWORD dwSize = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &dwSize, WINHTTP_NO_HEADER_INDEX);
        if (dwSize > 0) {
            vector<wchar_t> buf(dwSize / sizeof(wchar_t) + 1);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, buf.data(), &dwSize, WINHTTP_NO_HEADER_INDEX);
            ct = buf.data();
        }
        is_file = is_content_type_file(hRequest);
        do {
            WinHttpQueryDataAvailable(hRequest, &dwSize);
            if (dwSize > 0) {
                vector<char> buf(dwSize);
                DWORD dwRead = 0;
                WinHttpReadData(hRequest, buf.data(), dwSize, &dwRead);
                result.append(buf.data(), dwRead);
            }
        } while (dwSize > 0);
    }
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return { result, is_file, ct };
}

// 提取超链接
vector<string> extract_links(const string& html) {
    vector<string> links;
    regex href_regex(R"(<a\s[^>]*href=["']([^"']+)["'])");
    auto begin = sregex_iterator(html.begin(), html.end(), href_regex);
    auto end = sregex_iterator();
    for (auto it = begin; it != end; ++it)
        links.push_back((*it)[1]);
    return links;
}

// 绝对URL处理
string abs_url(const string& link, const string& host, INTERNET_SCHEME scheme) {
    if (link.find("http://") == 0 || link.find("https://") == 0) return link;
    string proto = (scheme == INTERNET_SCHEME_HTTPS) ? "https://" : "http://";
    if (!link.empty() && link[0] == '/')
        return proto + host + link;
    else
        return proto + host + "/" + link;
}

// 获取文件扩展名（路径优先，参数次之）
string get_extension_from_url(const wstring& path, const string& url, const wstring& content_type) {
    // 1. 路径部分
    size_t last_slash = path.find_last_of(L"/\\");
    wstring fname = (last_slash == wstring::npos) ? path : path.substr(last_slash + 1);
    size_t dot = fname.find_last_of(L'.');
    if (dot != wstring::npos && dot + 1 < fname.length()) {
        wstring ext = fname.substr(dot);
        return string(ext.begin(), ext.end());
    }
    // 2. 参数部分（如 f=JPEG 或 fmt=png）
    smatch m;
    regex re_f(R"([?&]f=([A-Za-z0-9]+))");
    if (regex_search(url, m, re_f)) {
        string val = m[1];
		//jpeg、png、gif、bmp、webp、svg、mp4、mp3、wav、ogg、pdf、zip、rar、txt、doc、docx、ppt、pptx
        if (val == "jpeg" || val == "jpg" || val == "JPEG" ||val == "JPG")
            return ".jpg";
        if (val == "png" || val == "PNG")
            return ".png";
        if (val == "gif" || val == "GIF")
            return ".gif";
        if (val == "bmp" || val == "BMP")
            return ".bmp";
        if (val == "webp" || val == "WEBP")
            return ".webp";
        if (val == "svg" || val == "SVG")
            return ".svg";
        if (val == "mp4" || val == "MP4")
            return ".mp4";
        if (val == "mp3" || val == "MP3")
			return ".mp3";
		if (val == "wav" || val == "WAV")
            return ".wav";
		if (val == "ogg" || val == "OGG")
            return ".ogg";
        if (val == "pdf" || val == "PDF")
            return ".pdf";
        if (val == "zip" || val == "ZIP")
			return ".zip";
		if (val == "rar" || val == "RAR")
            return ".rar";
        if (val == "txt" || val == "TXT")
            return ".txt";
        if (val == "doc" || val == "DOC")
            return ".doc";
        if (val == "docx" || val == "DOCX")
            return ".docx";
        if (val == "ppt" || val == "PPT")
            return ".ppt";
        if (val == "pptx" || val == "PPTX")
			return ".pptx";
    }
    regex re_fmt(R"([?&]fmt=([A-Za-z0-9]+))");
    if (regex_search(url, m, re_fmt)) {
        string val = m[1];
        if (val == "auto")
            return ""; // 不确定类型
        //jpeg、png、gif、bmp、webp、svg、mp4、mp3、wav、ogg、pdf、zip、rar、txt、doc、docx、ppt、pptx
        if (val == "jpeg" || val == "jpg" || val == "JPEG" || val == "JPG")
			return ".jpg";
        if (val == "png" || val == "PNG")
            return ".png";
        if (val == "gif" || val == "GIF")
            return ".gif";
        if (val == "bmp" || val == "BMP")
            return ".bmp";
        if (val == "webp" || val == "WEBP")
            return ".webp";
        if (val == "svg" || val == "SVG")
            return ".svg";
        if (val == "mp4" || val == "MP4")
            return ".mp4";
		if (val == "mp3" || val == "MP3")
			return ".mp3";
		if (val == "wav" || val == "WAV")
			return ".wav";
        if (val == "ogg" || val == "OGG")
            return ".ogg";
        if (val == "pdf" || val == "PDF")
            return ".pdf";
		if (val == "zip" || val == "ZIP")
			return ".zip";
		if (val == "rar" || val == "RAR")
            return ".rar";
        if (val == "txt" || val == "TXT")
            return ".txt";
        if (val == "doc" || val == "DOC")
            return ".doc";
        if (val == "docx" || val == "DOCX")
            return ".docx";
        if (val == "ppt" || val == "PPT")
            return ".ppt";
        if (val == "pptx" || val == "PPTX")
			return ".pptx";
    }
    // 3. Content-Type
    if (!content_type.empty()) {
        string ext = ext_from_content_type(content_type);
        if (ext != ".file") 
            return ext;
    }
    // 4. 默认
    return ".file";
}

// 保存文件
void save_to_file(const string& folder, const string& filename, const string& content) {
    filesystem::create_directory(folder);
    string filepath = folder + "/" + filename;
    ofstream ofs(filepath, ios::binary);
    ofs << content;
}

int main() {
    system("title pach爬虫v1.0");
	system("color 0A");
    cout << "作者：hs_luyuyang/luyuyang971\n";
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_FONT_INFOEX cfi = { sizeof(CONSOLE_FONT_INFOEX) };
    GetCurrentConsoleFontEx(hConsole, FALSE, &cfi);
    cfi.dwFontSize = { 12, 30 };
    cfi.FontWeight = FW_NORMAL;
    wcscpy_s(cfi.FaceName, L"Consolas");
    SetCurrentConsoleFontEx(hConsole, FALSE, &cfi);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    COORD bufferSize = { csbi.dwSize.X, 32768 };
    SetConsoleScreenBufferSize(hConsole, bufferSize);
    string start_url;
    cout << "请输入要爬取的网页URL（支持http和https）：";
    getline(cin, start_url);
    set<string> visited;
    vector<string> queue{ start_url };
    int file_count = 0;
    filesystem::create_directory("item");
    while (!queue.empty()) {
        string url = queue.back(); queue.pop_back();
        if (visited.count(url))
            continue;
        visited.insert(url);
        wstring host, path;
        INTERNET_SCHEME scheme;
        parse_url(url, host, path, scheme);
        cout << "正在爬取: " << url << endl;
        HttpResult httpres = winhttp_get_and_type(host, path, scheme);
        if (httpres.data.empty()) {
            cout << "获取失败，跳过" << endl;
            continue;
        }
        if (httpres.is_file) {
            string ext = get_extension_from_url(path, url, httpres.content_type);
            string fname = "download_" + to_string(file_count + 1) + ext;
            save_to_file("item", fname, httpres.data);
            cout << "已下载文件: " << fname << endl;
            ++file_count;
        }
        else {
            save_to_file("item", "page_" + to_string(visited.size()) + ".html", httpres.data);
            auto links = extract_links(httpres.data);
            for (const auto& link : links) {
                string next_url = abs_url(link, string(host.begin(), host.end()), scheme);
                if (!visited.count(next_url))
                    queue.push_back(next_url);
            }
        }
        this_thread::sleep_for(chrono::milliseconds(1200));
    }
    cout << "爬取完成，已下载 " << file_count << " 个文件。\n";
    _getch();
    return 0;
}