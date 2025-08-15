#include <iostream>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <functional>
#include <boost/algorithm/string.hpp>
#include "http_utils.h"
#include "database.h"
#include "config_parser.h"

// Глобальные переменные для пула потоков
std::mutex mtx;
std::condition_variable cv;
std::queue<std::function<void()>> tasks;
bool exitThreadPool = false;

void threadPoolWorker() {
    std::unique_lock<std::mutex> lock(mtx);
    while (!exitThreadPool || !tasks.empty()) {
        if (tasks.empty()) {
            cv.wait(lock);
        }
        else {
            auto task = tasks.front();
            tasks.pop();
            lock.unlock();
            task();
            lock.lock();
        }
    }
}

void processLink(const Link& link, int depth, Database& db) {
    try {
        std::cout << "Processing: " << link.hostName << link.query << " (depth: " << depth << ")\n";

        std::string html = getHtmlContent(link);
        if (html.empty()) {
            std::cerr << "Failed to get content from: " << link.hostName << link.query << "\n";
            return;
        }

        // Парсинг заголовка
        size_t titleStart = html.find("<title>");
        size_t titleEnd = html.find("</title>");
        std::string title = (titleStart != std::string::npos && titleEnd != std::string::npos) ?
            html.substr(titleStart + 7, titleEnd - (titleStart + 7)) : "No title";

        // Сохраняем документ в БД
        std::string fullUrl = (link.protocol == ProtocolType::HTTPS ? "https://" : "http://") +
            link.hostName + link.query;
        db.saveDocument(fullUrl, title, html);

        // Извлечение ссылок
        sregex_iterator it(html.begin(), html.end(), regex("<a\\s+[^>]*href=\"([^\"]*)\""));
        sregex_iterator end;

        vector<Link> new_links;
        for (; it != end; ++it) {
            string url = (*it)[1].str();
            if (url.empty() || url[0] == '#' || url.find("javascript:") == 0) continue;

            // Нормализация URL
            if (url.find("http") != 0) {
                url = (url[0] == '/')
                    ? (link.protocol == ProtocolType::HTTPS ? "https://" : "http://")
                    + link.hostName + url
                    : (link.protocol == ProtocolType::HTTPS ? "https://" : "http://")
                    + link.hostName + "/" + url;
            }

            // Парсинг URL
            smatch url_match;
            if (regex_match(url, url_match, regex("(https?)://([^/]+)(.*)"))) {
                Link new_link{
                    url_match[1] == "https" ? ProtocolType::HTTPS : ProtocolType::HTTP,
                    url_match[2].str(),
                    url_match[3].str().empty() ? "/" : url_match[3].str()
                };
                new_links.push_back(new_link);
            }
        }

        // Добавление новых ссылок в очередь
        if (depth > 0) {
            lock_guard<mutex> lock(mtx);
            for (auto& new_link : new_links) {
                tasks.push([new_link, depth, &db]() {
                    processLink(new_link, depth - 1, db);
                    });
            }
            cv.notify_one();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error processing link: " << e.what() << "\n";
    }
}

int main() {
    try {
        // Загрузка конфигурации
        ConfigParser config("config.ini");

        // Инициализация БД
        Database db(
            config.get("database", "host"),
            config.get("database", "port"),
            config.get("database", "dbname"),
            config.get("database", "user"),
            config.get("database", "password")
        );

        // Настройка пула потоков
        int numThreads = config.getInt("spider", "num_threads");
        int maxDepth = config.getInt("spider", "max_depth");

        std::vector<std::thread> threadPool;
        for (int i = 0; i < numThreads; ++i) {
            threadPool.emplace_back(threadPoolWorker);
        }

        // Стартовая ссылка
        std::string startUrl = config.get("spider", "start_url");
        Link startLink;

        if (startUrl.find("https://") == 0) {
            startLink.protocol = ProtocolType::HTTPS;
            startUrl = startUrl.substr(8);
        }
        else {
            startLink.protocol = ProtocolType::HTTP;
            startUrl = startUrl.substr(7);
        }

        size_t pathPos = startUrl.find('/');
        startLink.hostName = startUrl.substr(0, pathPos);
        startLink.query = pathPos != std::string::npos ? startUrl.substr(pathPos) : "/";

        // Запуск обработки
        {
            std::lock_guard<std::mutex> lock(mtx);
            tasks.push([startLink, maxDepth, &db]() {
                processLink(startLink, maxDepth, db);
                });
            cv.notify_one();
        }

        // Ожидание завершения
        std::this_thread::sleep_for(std::chrono::seconds(10));

        // Завершение работы
        {
            std::lock_guard<std::mutex> lock(mtx);
            exitThreadPool = true;
            cv.notify_all();
        }

        for (auto& t : threadPool) {
            t.join();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}