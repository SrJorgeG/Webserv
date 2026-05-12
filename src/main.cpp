#include "Webserv.hpp"
#include "config/ConfigParser.hpp"
#include "core/Reactor.hpp"
#include "utils/Logger.hpp"
#include "utils/StringUtils.hpp"

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);

    std::string configPath = DEFAULT_CONFIG_PATH;

    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " [configuration file]" << std::endl;
        return 1;
    }
    if (argc == 2) {
        configPath = argv[1];
    }

    Logger::getInstance().setLevel(LOG_INFO);
    LOG_INFO("Starting webserv...");

    try {
        ConfigParser parser;
        std::vector<ServerConfig> servers = parser.parse(configPath);

        LOG_INFO("Configuration loaded successfully.");
        LOG_INFO("Starting server with " + StringUtils::intToString(servers.size()) + " server block(s).");

        Reactor reactor;
        if (!reactor.init(servers)) {
            LOG_ERROR("Failed to initialize reactor.");
            return 1;
        }

        LOG_INFO("Server running. Press Ctrl+C to stop.");
        reactor.run();

    } catch (const std::exception& e) {
        LOG_ERROR(std::string("Fatal error: ") + e.what());
        return 1;
    }

    LOG_INFO("Server stopped.");
    return 0;
}
