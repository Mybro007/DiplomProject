#include <iostream>
#include <chrono>
#include <Windows.h>
#include "http_connection.h"
#include "database.h"
#include "config_parser.h"
#include "search_handler.h"

void runServer(tcp::acceptor& acceptor, tcp::socket& socket, Database& db) {
    acceptor.async_accept(socket,
        [&](beast::error_code ec) {
            if (!ec) {
                std::make_shared<HttpConnection>(std::move(socket), db)->start();
            }
            runServer(acceptor, socket, db);
        });
}

int main(int argc, char* argv[]) {
    try {
        // Настройки локали для Windows
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);

        // Загрузка конфигурации
        ConfigParser config("config.ini");

        // Инициализация БД
        Database db(config.get("database", "connection_string"));

        // Настройка сервера
        auto const address = net::ip::make_address("0.0.0.0");
        unsigned short port = config.getInt("server", "port");

        net::io_context ioc{ 1 };
        tcp::acceptor acceptor{ ioc, {address, port} };
        tcp::socket socket{ ioc };

        // Запуск сервера
        runServer(acceptor, socket, db);

        std::cout << "Search server started on http://localhost:" << port << std::endl;
        std::cout << "Press Ctrl+C to stop..." << std::endl;

        ioc.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}