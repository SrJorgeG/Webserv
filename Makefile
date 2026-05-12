NAME = webserv

CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98 -I./include -MMD -MP
DEPS = $(OBJS:.o=.d)

SRC_DIR = src
OBJ_DIR = obj

SRCS = \
	$(SRC_DIR)/main.cpp \
	$(SRC_DIR)/core/Reactor.cpp \
	$(SRC_DIR)/core/ServerSocket.cpp \
	$(SRC_DIR)/core/Connection.cpp \
	$(SRC_DIR)/http/Request.cpp \
	$(SRC_DIR)/http/Response.cpp \
	$(SRC_DIR)/http/HttpParser.cpp \
	$(SRC_DIR)/http/StatusCodes.cpp \
	$(SRC_DIR)/http/GetHandler.cpp \
	$(SRC_DIR)/http/PostHandler.cpp \
	$(SRC_DIR)/http/DeleteHandler.cpp \
	$(SRC_DIR)/http/SessionManager.cpp \
	$(SRC_DIR)/config/ConfigParser.cpp \
	$(SRC_DIR)/config/ServerConfig.cpp \
	$(SRC_DIR)/config/RouteConfig.cpp \
	$(SRC_DIR)/cgi/CgiHandler.cpp \
	$(SRC_DIR)/utils/StringUtils.cpp \
	$(SRC_DIR)/utils/Logger.cpp \
	$(SRC_DIR)/utils/FileUtils.cpp

OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

all: directories $(NAME)

directories:
	@mkdir -p $(OBJ_DIR)/core $(OBJ_DIR)/http $(OBJ_DIR)/config $(OBJ_DIR)/cgi $(OBJ_DIR)/utils

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re directories
